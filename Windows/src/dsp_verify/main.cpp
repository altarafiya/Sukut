#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dsp/AudioLevel.h"
#include "dsp/OfflineCleaner.h"
#include "dsp/StreamingDsp.h"
#include "kiss_fftr.h"
#include "offline/OfflinePipeline.h"
#include "offline/WavIO.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register_ref.h"
#include "tensorflow/lite/model_builder.h"

namespace {

using Dsp = cleanwave::StreamingDsp;

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename A, typename B>
double MaxAbsDifference(const A& a, const B& b) {
  Require(a.size() == b.size(), "comparison size mismatch");
  double result = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    result = std::max(
        result, std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
  }
  return result;
}

template <typename Container>
double MaxAbs(const Container& values) {
  double result = 0.0;
  for (const auto value : values) {
    result = std::max(result, std::abs(static_cast<double>(value)));
  }
  return result;
}

std::vector<float> LoadWindowIndependent(const std::filesystem::path& path,
                                         std::size_t count) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("independent window open failed");
  std::vector<unsigned char> bytes(count * 2);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream || stream.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("independent window size mismatch");
  }
  std::vector<float> result(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint16_t packed = static_cast<std::uint16_t>(bytes[i * 2]) |
                                 (static_cast<std::uint16_t>(bytes[i * 2 + 1]) << 8U);
    result[i] = static_cast<float>(static_cast<std::int16_t>(packed)) / 32768.0F;
  }
  return result;
}

class KissRealFft final {
 public:
  KissRealFft() {
    forward_ = kiss_fftr_alloc(static_cast<int>(Dsp::kFftSize), 0, nullptr, nullptr);
    inverse_ = kiss_fftr_alloc(static_cast<int>(Dsp::kFftSize), 1, nullptr, nullptr);
    if (!forward_ || !inverse_) throw std::runtime_error("KissFFT plan allocation failed");
  }
  ~KissRealFft() {
    if (forward_) kiss_fftr_free(forward_);
    if (inverse_) kiss_fftr_free(inverse_);
  }

  std::array<kiss_fft_cpx, Dsp::kSpectrumBins> Forward(
      const std::array<float, Dsp::kFftSize>& input) {
    std::array<kiss_fft_cpx, Dsp::kSpectrumBins> result{};
    kiss_fftr(forward_, input.data(), result.data());
    return result;
  }

  std::array<float, Dsp::kFftSize> Inverse(
      const std::array<kiss_fft_cpx, Dsp::kSpectrumBins>& spectrum) {
    std::array<float, Dsp::kFftSize> result{};
    kiss_fftri(inverse_, spectrum.data(), result.data());
    for (float& value : result) value /= static_cast<float>(Dsp::kFftSize);
    return result;
  }

 private:
  kiss_fftr_cfg forward_ = nullptr;
  kiss_fftr_cfg inverse_ = nullptr;
};

struct ReferenceModelOutputs {
  cleanwave::SeparationModel::SpectralBuffer voice_left{};
  cleanwave::SeparationModel::SpectralBuffer voice_right{};
  cleanwave::SeparationModel::StateBuffer state{};
  cleanwave::SeparationModel::SpectralBuffer music_left{};
  cleanwave::SeparationModel::SpectralBuffer music_right{};
};

class ReferenceModel final {
 public:
  explicit ReferenceModel(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("reference model open failed");
    const auto size = stream.tellg();
    if (size <= 0) throw std::runtime_error("reference model is empty");
    bytes_.resize(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes_.data()), size);
    if (!stream) throw std::runtime_error("reference model read failed");
    model_ = tflite::FlatBufferModel::VerifyAndBuildFromBuffer(
        reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
    if (!model_) throw std::runtime_error("reference model verification failed");
    tflite::InterpreterBuilder builder(*model_, resolver_);
    if (builder.SetNumThreads(1) != kTfLiteOk ||
        builder(&interpreter_) != kTfLiteOk || !interpreter_ ||
        interpreter_->AllocateTensors() != kTfLiteOk) {
      throw std::runtime_error("reference model interpreter creation failed");
    }
  }

  std::unique_ptr<ReferenceModelOutputs> Run(
      const cleanwave::SeparationModel::SpectralBuffer& left,
      const cleanwave::SeparationModel::SpectralBuffer& right,
      const cleanwave::SeparationModel::StateBuffer& state) {
    std::memcpy(interpreter_->typed_input_tensor<float>(0), left.data(), sizeof(left));
    std::memcpy(interpreter_->typed_input_tensor<float>(1), right.data(), sizeof(right));
    std::memcpy(interpreter_->typed_input_tensor<float>(2), state.data(), sizeof(state));
    if (interpreter_->Invoke() != kTfLiteOk) {
      throw std::runtime_error("reference model invoke failed");
    }
    auto result = std::make_unique<ReferenceModelOutputs>();
    Copy(0, result->voice_left);
    Copy(1, result->voice_right);
    Copy(2, result->state);
    Copy(3, result->music_left);
    Copy(4, result->music_right);
    return result;
  }

 private:
  template <typename Container>
  void Copy(int output, Container& destination) {
    const float* source = interpreter_->typed_output_tensor<float>(output);
    if (!source) throw std::runtime_error("reference output type mismatch");
    std::copy_n(source, destination.size(), destination.begin());
  }

  tflite::ops::builtin::BuiltinRefOpResolver resolver_;
  std::vector<std::uint8_t> bytes_;
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::Interpreter> interpreter_;
};

std::complex<long double> DirectDftBin(
    const std::array<float, Dsp::kFftSize>& input, std::size_t bin) {
  std::complex<long double> sum{0.0L, 0.0L};
  const long double factor =
      -2.0L * std::numbers::pi_v<long double> * static_cast<long double>(bin) /
      static_cast<long double>(Dsp::kFftSize);
  for (std::size_t n = 0; n < Dsp::kFftSize; ++n) {
    const long double phase = factor * static_cast<long double>(n);
    sum += static_cast<long double>(input[n]) *
           std::complex<long double>(std::cos(phase), std::sin(phase));
  }
  return sum;
}

struct NumericMetrics {
  double max_abs = 0.0;
  double rms = 0.0;
  double reference_max_abs = 0.0;
  double reference_rms = 0.0;
  double max_element_bound_ratio = 0.0;
};

template <typename A, typename B>
NumericMetrics CompareModelValues(const A& actual, const B& reference) {
  Require(actual.size() == reference.size(), "model comparison size mismatch");
  long double error_sq = 0.0L;
  long double reference_sq = 0.0L;
  NumericMetrics metrics;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    Require(std::isfinite(actual[i]) && std::isfinite(reference[i]),
            "non-finite model comparison value");
    const double a = actual[i];
    const double r = reference[i];
    const double error = std::abs(a - r);
    metrics.max_abs = std::max(metrics.max_abs, error);
    metrics.reference_max_abs = std::max(metrics.reference_max_abs, std::abs(r));
    error_sq += static_cast<long double>(error) * error;
    reference_sq += static_cast<long double>(r) * r;
    const double bound = 5.0e-4 + 1.0e-2 * std::abs(r);
    metrics.max_element_bound_ratio =
        std::max(metrics.max_element_bound_ratio, error / bound);
  }
  metrics.rms = std::sqrt(static_cast<double>(error_sq / actual.size()));
  metrics.reference_rms =
      std::sqrt(static_cast<double>(reference_sq / actual.size()));
  return metrics;
}

bool ModelParityPass(const NumericMetrics& metrics) {
  const double peak_ratio =
      metrics.max_abs / std::max(metrics.reference_max_abs, 1.0e-12);
  const double rms_ratio =
      metrics.rms / std::max(metrics.reference_rms, 1.0e-12);
  return peak_ratio <= 0.01 && rms_ratio <= 0.01 &&
         metrics.max_element_bound_ratio <= 1.0;
}

double SpectrumMaxAbsDifference(const Dsp::Spectrum& a,
                                const Dsp::Spectrum& b) {
  double result = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    result = std::max(result,
                      std::abs(static_cast<double>(a[i].real) - b[i].real));
    result = std::max(result,
                      std::abs(static_cast<double>(a[i].imag) - b[i].imag));
  }
  return result;
}

Dsp::Spectrum DirectSpectrumFromStereo(std::span<const float> frame,
                                       std::size_t channel,
                                       const std::vector<float>& rise,
                                       const std::vector<float>& hop) {
  Require(frame.size() == Dsp::kFftSize * 2,
          "direct spectrum frame size mismatch");
  Dsp::Spectrum spectrum{};
  for (std::size_t bin = 0; bin < Dsp::kSpectrumBins; ++bin) {
    std::complex<long double> sum{0.0L, 0.0L};
    const long double factor =
        -2.0L * std::numbers::pi_v<long double> *
        static_cast<long double>(bin) / static_cast<long double>(Dsp::kFftSize);
    for (std::size_t n = 0; n < Dsp::kFftSize; ++n) {
      const float window = n < Dsp::kOverlapFrames
                               ? rise[n]
                               : hop[n - Dsp::kOverlapFrames];
      const long double sample =
          static_cast<long double>(frame[n * 2 + channel] * window);
      const long double phase = factor * static_cast<long double>(n);
      sum += sample *
             std::complex<long double>(std::cos(phase), std::sin(phase));
    }
    spectrum[bin].real = static_cast<float>(sum.real());
    spectrum[bin].imag =
        (bin == 0 || bin == Dsp::kFftSize / 2)
            ? 0.0F
            : static_cast<float>(sum.imag());
  }
  return spectrum;
}

cleanwave::SeparationModel::SpectralBuffer IndependentPackModelInput(
    const Dsp::Spectrum& spectrum) {
  cleanwave::SeparationModel::SpectralBuffer result{};
  const float scale = 32768.0F / 32767.0F;
  for (std::size_t bin = 0; bin < Dsp::kModelBins; ++bin) {
    result[bin * 2] = spectrum[bin].real * scale;
    result[bin * 2 + 1] = spectrum[bin].imag * scale;
  }
  return result;
}

Dsp::Spectrum IndependentRemix(
    const Dsp::Spectrum& original,
    const cleanwave::SeparationModel::SpectralBuffer& voice,
    const cleanwave::SeparationModel::SpectralBuffer& music, int voice_q12,
    int music_q12, int other_q12) {
  Dsp::Spectrum result{};
  const float reverse = 32767.0F / 32768.0F;
  const float gv = static_cast<float>(voice_q12) / 4096.0F;
  const float gm = static_cast<float>(music_q12) / 4096.0F;
  const float go = static_cast<float>(other_q12) / 4096.0F;
  for (std::size_t bin = 0; bin < Dsp::kSpectrumBins; ++bin) {
    const float vr = bin < Dsp::kModelBins ? voice[bin * 2] * reverse : 0.0F;
    const float vi =
        bin < Dsp::kModelBins ? voice[bin * 2 + 1] * reverse : 0.0F;
    const float mr = bin < Dsp::kModelBins ? music[bin * 2] * reverse : 0.0F;
    const float mi =
        bin < Dsp::kModelBins ? music[bin * 2 + 1] * reverse : 0.0F;
    result[bin].real = vr * gv + mr * gm + (original[bin].real - vr - mr) * go;
    const float imag = vi * gv + mi * gm + (original[bin].imag - vi - mi) * go;
    result[bin].imag =
        (bin == 0 || bin == Dsp::kFftSize / 2) ? 0.0F : imag;
  }
  return result;
}

std::array<float, Dsp::kFftSize> DirectInverse(
    const Dsp::Spectrum& spectrum) {
  std::array<float, Dsp::kFftSize> result{};
  const long double scale = 1.0L / static_cast<long double>(Dsp::kFftSize);
  for (std::size_t n = 0; n < Dsp::kFftSize; ++n) {
    long double value = spectrum[0].real;
    value += static_cast<long double>(spectrum[Dsp::kFftSize / 2].real) *
             ((n & 1U) ? -1.0L : 1.0L);
    for (std::size_t bin = 1; bin < Dsp::kFftSize / 2; ++bin) {
      const long double phase =
          2.0L * std::numbers::pi_v<long double> *
          static_cast<long double>(bin * n) /
          static_cast<long double>(Dsp::kFftSize);
      value += 2.0L *
               (static_cast<long double>(spectrum[bin].real) * std::cos(phase) -
                static_cast<long double>(spectrum[bin].imag) * std::sin(phase));
    }
    result[n] = static_cast<float>(value * scale);
  }
  return result;
}

Dsp::HopStereo IndependentSynthesize(
    const std::array<float, Dsp::kFftSize>& left,
    const std::array<float, Dsp::kFftSize>& right,
    const std::vector<float>& rise, const std::vector<float>& hop,
    Dsp::HopStereo& tail) {
  Dsp::HopStereo output{};
  for (std::size_t i = 0; i < Dsp::kOverlapFrames; ++i) {
    output[i * 2] =
        tail[i * 2] * hop[Dsp::kDirectFrames + i] + left[i] * rise[i];
    output[i * 2 + 1] =
        tail[i * 2 + 1] * hop[Dsp::kDirectFrames + i] + right[i] * rise[i];
  }
  for (std::size_t i = Dsp::kOverlapFrames; i < Dsp::kHopFrames; ++i) {
    output[i * 2] = left[i] * hop[i - Dsp::kOverlapFrames];
    output[i * 2 + 1] = right[i] * hop[i - Dsp::kOverlapFrames];
  }
  for (std::size_t i = 0; i < Dsp::kOverlapFrames; ++i) {
    tail[i * 2] = left[Dsp::kHopFrames + i];
    tail[i * 2 + 1] = right[Dsp::kHopFrames + i];
  }
  return output;
}

std::vector<float> MakeStereoFloat(std::size_t frames) {
  std::vector<float> result(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    result[i * 2] = static_cast<float>(
        0.23 * std::sin(2.0 * std::numbers::pi * 997.0 * t) +
        0.11 * std::sin(2.0 * std::numbers::pi * 4213.0 * t));
    result[i * 2 + 1] = static_cast<float>(
        0.19 * std::sin(2.0 * std::numbers::pi * 1777.0 * t + 0.31) -
        0.07 * std::sin(2.0 * std::numbers::pi * 6131.0 * t));
  }
  return result;
}

std::vector<std::int16_t> MakeStereoPcm(std::size_t frames) {
  std::vector<std::int16_t> result(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    const double left = 0.36 * std::sin(2.0 * std::numbers::pi * 997.0 * t) +
                        0.08 * std::sin(2.0 * std::numbers::pi * 3031.0 * t);
    const double right = 0.27 * std::sin(2.0 * std::numbers::pi * 4213.0 * t + 0.41) -
                         0.05 * std::sin(2.0 * std::numbers::pi * 811.0 * t);
    result[i * 2] = static_cast<std::int16_t>(std::lround(left * 30000.0));
    result[i * 2 + 1] = static_cast<std::int16_t>(std::lround(right * 30000.0));
  }
  return result;
}

struct VerificationMetrics {
  double window_decode_max = 0.0;
  double window_complement_relative = 0.0;
  double dft_max_complex_error = 0.0;
  double fft_roundtrip_max_error = 0.0;
  double steady_wola_max_error = 0.0;
  double production_first_transient_max_error = 0.0;
  double production_steady_identity_max_error = 0.0;
  double reset_replay_max_error = 0.0;
  double reset_replay_state_max_error = 0.0;
  double recurrent_state_reset_delta = 0.0;
  double integrated_feature_max_abs = 0.0;
  double integrated_model_head_max_abs = 0.0;
  double integrated_model_state_max_abs = 0.0;
  double integrated_remix_max_abs = 0.0;
  double integrated_final_wet_max_abs = 0.0;
  double processed_eof_reference_max_pcm_delta = 0.0;
  std::int32_t actual_model_max_pcm_delta = 0;
};

void VerifyWindowsAndFft(const std::filesystem::path& rise_path,
                         const std::filesystem::path& hop_path,
                         VerificationMetrics& metrics) {
  const auto rise = LoadWindowIndependent(rise_path, Dsp::kOverlapFrames);
  const auto hop = LoadWindowIndependent(hop_path, Dsp::kHopFrames);
  const auto production_rise =
      Dsp::DecodeQ15Window(rise_path, Dsp::kOverlapFrames);
  const auto production_hop = Dsp::DecodeQ15Window(hop_path, Dsp::kHopFrames);
  metrics.window_decode_max = std::max(MaxAbsDifference(rise, production_rise),
                                       MaxAbsDifference(hop, production_hop));
  Require(metrics.window_decode_max == 0.0, "Q15 window decode mismatch");

  const double target = static_cast<double>(32767LL * 32767LL);
  for (std::size_t i = 0; i < Dsp::kOverlapFrames; ++i) {
    const long long a = static_cast<long long>(rise[i] * 32768.0F);
    const long long b =
        static_cast<long long>(hop[Dsp::kDirectFrames + i] * 32768.0F);
    metrics.window_complement_relative =
        std::max(metrics.window_complement_relative,
                 std::abs(static_cast<double>(a * a + b * b) - target) / target);
  }
  Require(metrics.window_complement_relative < 0.0002,
          "Audio window complement invariant failed");

  std::array<float, Dsp::kFftSize> signal{};
  for (std::size_t i = 0; i < signal.size(); ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    signal[i] = static_cast<float>(
        0.31 * std::sin(2.0 * std::numbers::pi * 997.0 * t) +
        0.17 * std::sin(2.0 * std::numbers::pi * 4213.0 * t) +
        (i == 137 ? 0.123 : 0.0));
  }
  KissRealFft fft;
  const auto spectrum = fft.Forward(signal);
  const std::array<std::size_t, 7> bins = {0, 1, 17, 127, 511, 600, 768};
  for (const std::size_t bin : bins) {
    const auto reference = DirectDftBin(signal, bin);
    const double re_error =
        std::abs(static_cast<double>(spectrum[bin].r) -
                 static_cast<double>(reference.real()));
    const double im_error =
        std::abs(static_cast<double>(spectrum[bin].i) -
                 static_cast<double>(reference.imag()));
    metrics.dft_max_complex_error =
        std::max({metrics.dft_max_complex_error, re_error, im_error});
    const double magnitude = static_cast<double>(std::abs(reference));
    const double allowed = 0.002 + magnitude * 2.0e-5;
    Require(re_error <= allowed && im_error <= allowed,
            "KissFFT forward differs from scalar DFT");
  }
  const auto roundtrip = fft.Inverse(spectrum);
  metrics.fft_roundtrip_max_error = MaxAbsDifference(signal, roundtrip);
  Require(metrics.fft_roundtrip_max_error < 5.0e-6,
          "KissFFT normalized inverse round-trip failed");

  const std::size_t total = Dsp::kFftSize + Dsp::kHopFrames;
  std::vector<float> long_signal(total);
  for (std::size_t i = 0; i < total; ++i) {
    long_signal[i] = static_cast<float>(
        0.31 * std::sin(2.0 * std::numbers::pi * 997.0 * i / 48000.0) +
        0.17 * std::sin(2.0 * std::numbers::pi * 4213.0 * i / 48000.0));
  }
  const auto round_trip_windowed = [&](std::size_t start) {
    std::array<float, Dsp::kFftSize> frame{};
    for (std::size_t i = 0; i < Dsp::kFftSize; ++i) {
      const float window = i < Dsp::kOverlapFrames
                               ? rise[i]
                               : hop[i - Dsp::kOverlapFrames];
      frame[i] = long_signal[start + i] * window;
    }
    return fft.Inverse(fft.Forward(frame));
  };
  const auto first = round_trip_windowed(0);
  const auto second = round_trip_windowed(Dsp::kHopFrames);
  for (std::size_t i = 0; i < Dsp::kHopFrames; ++i) {
    float reconstructed = 0.0F;
    if (i < Dsp::kOverlapFrames) {
      reconstructed = first[Dsp::kHopFrames + i] *
                          hop[Dsp::kDirectFrames + i] +
                      second[i] * rise[i];
    } else {
      reconstructed = second[i] * hop[i - Dsp::kOverlapFrames];
    }
    metrics.steady_wola_max_error =
        std::max(metrics.steady_wola_max_error,
                 std::abs(static_cast<double>(reconstructed) -
                          long_signal[Dsp::kHopFrames + i]));
  }
  Require(metrics.steady_wola_max_error < 5.0e-5,
          "steady-state WOLA identity exceeds Android reference guard");
}

void VerifyPackAndRemix() {
  Dsp::Spectrum spectrum{};
  spectrum[0] = {1.0F, 7.0F};
  spectrum[511] = {-2.0F, 3.0F};
  spectrum[512] = {9.0F, -4.0F};
  const auto packed = Dsp::PackModelInput(spectrum);
  const float scale = 32768.0F / 32767.0F;
  Require(packed[0] == spectrum[0].real * scale,
          "model feature scale mismatch at bin 0 real");
  Require(packed[1] == spectrum[0].imag * scale,
          "model feature scale mismatch at bin 0 imag");
  Require(packed[1022] == spectrum[511].real * scale &&
              packed[1023] == spectrum[511].imag * scale,
          "model feature boundary mismatch at bin 511");

  cleanwave::SeparationModel::SpectralBuffer voice{};
  cleanwave::SeparationModel::SpectralBuffer music{};
  voice[20] = 0.5F;
  voice[21] = -0.25F;
  music[20] = -0.125F;
  music[21] = 0.0625F;
  spectrum[10] = {0.75F, -0.375F};
  spectrum[600] = {0.625F, -0.1875F};
  spectrum[768] = {0.4F, 8.0F};

  const auto residual_only = Dsp::RemixSpectrum(spectrum, voice, music, 0, 0, 4096);
  Require(residual_only[600].real == spectrum[600].real &&
              residual_only[600].imag == spectrum[600].imag,
          "upper band did not pass through residual-only path");
  Require(residual_only[768].imag == 0.0F, "Nyquist imaginary component was not zeroed");
  Require(residual_only[0].imag == 0.0F, "DC imaginary component was not zeroed");

  const auto upper_muted = Dsp::RemixSpectrum(spectrum, voice, music, 4096, 4096, 0);
  Require(upper_muted[600].real == 0.0F && upper_muted[600].imag == 0.0F,
          "upper band leaked into voice/music-only path");

  const float reverse = 32767.0F / 32768.0F;
  const float vr = voice[20] * reverse;
  const float vi = voice[21] * reverse;
  const float mr = music[20] * reverse;
  const float mi = music[21] * reverse;
  const float gv = 1295.0F / 4096.0F;
  const float gm = 410.0F / 4096.0F;
  const float go = 4096.0F / 4096.0F;
  const auto mixed = Dsp::RemixSpectrum(spectrum, voice, music, 1295, 410, 4096);
  const float expected_real =
      vr * gv + mr * gm + (spectrum[10].real - vr - mr) * go;
  const float expected_imag =
      vi * gv + mi * gm + (spectrum[10].imag - vi - mi) * go;
  Require(std::abs(mixed[10].real - expected_real) < 1.0e-7F &&
              std::abs(mixed[10].imag - expected_imag) < 1.0e-7F,
          "float Q12 residual remix mismatch");
}

void VerifyAudioLevel() {
  cleanwave::AudioLevel dry_level;
  dry_level.SetReductionPercent(0);
  dry_level.ResetAmount();
  Dsp::HopStereo dry{};
  Dsp::HopStereo wet{};
  cleanwave::AudioLevel::PcmHop output{};
  std::array<std::int16_t, Dsp::kHopFrames * 2> expected{};
  for (std::size_t i = 0; i < dry.size(); ++i) {
    expected[i] = static_cast<std::int16_t>(
        static_cast<int>((i * 997U) & 0xffffU) - 32768);
    dry[i] = static_cast<float>(expected[i]) / 32768.0F;
    wet[i] = (i & 1U) ? -0.7F : 0.8F;
  }
  dry_level.Render(dry, wet, output, false);
  Require(std::equal(output.begin(), output.end(), expected.begin()),
          "AudioLevel reduction=0 did not preserve exact PCM");

  cleanwave::AudioLevel ramp_level;
  dry.fill(0.0F);
  wet.fill(0.5F);
  ramp_level.Render(dry, wet, output, true);
  Require(output[0] > 15000, "paused wet-to-dry ramp did not start near wet");
  Require(output[600] == 0, "paused wet-to-dry ramp did not settle by 300 frames");

  cleanwave::AudioLevel limiter_level;
  dry.fill(0.0F);
  wet.fill(0.0F);
  wet[0] = 0.91F;
  wet[1] = -0.91F;
  wet[2] = 0.899F;
  wet[3] = -0.899F;
  limiter_level.Render(dry, wet, output, false);
  const auto limiter_pcm = [](float sample) {
    const float sign = sample < 0.0F ? -1.0F : 1.0F;
    const float limited =
        sign * (0.9F + 0.099F *
                           std::tanh((std::abs(sample) - 0.9F) / 0.099F));
    return static_cast<std::int16_t>(limited * 32768.0F);
  };
  Require(output[0] == limiter_pcm(0.91F) &&
              output[1] == limiter_pcm(-0.91F) &&
              output[2] == static_cast<std::int16_t>(0.899F * 32768.0F) &&
              output[3] == static_cast<std::int16_t>(-0.899F * 32768.0F),
          "AudioLevel tanh knee/negative clipping reference mismatch");

  cleanwave::AudioLevel compensation_level;
  dry.fill(0.5F);
  wet.fill(0.25F);
  compensation_level.Render(dry, wet, output, false);
  const auto compensation_expected = [](std::size_t frame) {
    // ratio=.5 => 1/ratio=2, capped to 1.5; next gain=1+.15*(.5)=1.075.
    const float level = 1.0F + 0.075F * static_cast<float>(frame) /
                                   static_cast<float>(Dsp::kHopFrames);
    return static_cast<std::int16_t>(0.25F * level * 32768.0F);
  };
  Require(output[0] == compensation_expected(0) &&
              output[1000] == compensation_expected(500) &&
              output[1918] == compensation_expected(959),
          "AudioLevel compensation cap/smoothing interpolation mismatch");

  cleanwave::AudioLevel boost_level;
  boost_level.SetVoiceBoostDb(6);
  dry.fill(0.2F);
  wet.fill(0.2F);
  boost_level.Render(dry, wet, output, false);
  const float boost_gain = static_cast<float>(std::pow(10.0, 6.0 / 20.0));
  const auto boosted_pcm =
      static_cast<std::int16_t>(0.2F * boost_gain * 32768.0F);
  Require(output[0] == boosted_pcm && output[1919] == boosted_pcm,
          "AudioLevel +6 dB boost mismatch");

  const std::array<float, 3> nonfinite_values = {
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()};
  for (const float invalid : nonfinite_values) {
    cleanwave::AudioLevel nonfinite_level;
    wet.fill(0.0F);
    wet[0] = invalid;
    bool rejected = false;
    try {
      nonfinite_level.Render(dry, wet, output, false);
    } catch (const std::exception&) {
      rejected = true;
    }
    Require(rejected, "AudioLevel accepted non-finite wet sample");
  }

  Require(cleanwave::OfflineCleaner::DbToQ12(-20) == 410 &&
              cleanwave::OfflineCleaner::DbToQ12(-10) == 1295 &&
              cleanwave::OfflineCleaner::DbToQ12(0) == 4096 &&
              cleanwave::OfflineCleaner::DbToQ12(6) == 8173 &&
              cleanwave::OfflineCleaner::DbToQ12(-100) == 0 &&
              cleanwave::OfflineCleaner::DbToQ12(10) == 12953 &&
              cleanwave::OfflineCleaner::DbToQ12(-101) == 0 &&
              cleanwave::OfflineCleaner::DbToQ12(11) == 12953,
          "Q12 dB conversion checkpoints mismatch");
}

std::vector<std::int16_t> ManualFiniteProcessedReference(
    cleanwave::OfflineCleaner& cleaner,
    std::span<const std::int16_t> input) {
  Require((input.size() % 2) == 0, "manual EOF input must be stereo aligned");
  const std::size_t frames = input.size() / 2;
  cleaner.ResetAll();
  std::vector<std::int16_t> output(frames * 2);
  if (frames == 0) return output;

  std::size_t read_frame = 0;
  std::size_t write_frame = 0;
  bool first = true;
  cleanwave::OfflineCleaner::PcmHop hop{};
  while (write_frame < frames) {
    const std::size_t packet_frames =
        first ? Dsp::kFirstInputFrames : Dsp::kHopFrames;
    std::vector<std::int16_t> packet(packet_frames * 2, 0);
    const std::size_t available =
        read_frame < frames ? std::min(packet_frames, frames - read_frame) : 0;
    if (available != 0) {
      std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(read_frame * 2),
                  available * 2, packet.begin());
      read_frame += available;
    }
    cleaner.ProcessPcm16(packet, first, hop, false, true);
    const std::size_t emit = std::min(Dsp::kHopFrames, frames - write_frame);
    std::copy_n(hop.begin(), emit * 2,
                output.begin() + static_cast<std::ptrdiff_t>(write_frame * 2));
    write_frame += emit;
    first = false;
  }
  return output;
}

void WriteOddUnknownChunkFixture(const std::filesystem::path& path,
                                 std::span<const std::int16_t> samples) {
  Require((samples.size() % 2) == 0, "odd chunk fixture must be stereo aligned");
  const std::uint32_t data_bytes =
      static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
  const std::uint32_t junk_size = 3;
  const std::uint32_t riff_size =
      4U + (8U + junk_size + 1U) + (8U + 16U) + (8U + data_bytes);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("cannot create odd-chunk WAV fixture");
  const auto u16 = [&](std::uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8U) & 0xffU)};
    stream.write(reinterpret_cast<const char*>(bytes), 2);
  };
  const auto u32 = [&](std::uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8U) & 0xffU),
        static_cast<unsigned char>((value >> 16U) & 0xffU),
        static_cast<unsigned char>((value >> 24U) & 0xffU)};
    stream.write(reinterpret_cast<const char*>(bytes), 4);
  };
  stream.write("RIFF", 4);
  u32(riff_size);
  stream.write("WAVE", 4);
  stream.write("JUNK", 4);
  u32(junk_size);
  stream.write("abc", 3);
  stream.put('\0');
  stream.write("fmt ", 4);
  u32(16);
  u16(1);
  u16(2);
  u32(48000);
  u32(48000 * 4);
  u16(4);
  u16(16);
  stream.write("data", 4);
  u32(data_bytes);
  for (const auto sample : samples) u16(static_cast<std::uint16_t>(sample));
  if (!stream) throw std::runtime_error("failed writing odd-chunk WAV fixture");
}

void VerifyProductionDsp(const std::filesystem::path& model,
                         const std::filesystem::path& rise_path,
                         const std::filesystem::path& hop_path,
                         VerificationMetrics& metrics) {
  const auto rise = LoadWindowIndependent(rise_path, Dsp::kOverlapFrames);
  const auto hop = LoadWindowIndependent(hop_path, Dsp::kHopFrames);
  const auto signal = MakeStereoFloat(Dsp::kFftSize + Dsp::kHopFrames);
  const std::span<const float> first(signal.data(), Dsp::kFftSize * 2);
  const std::span<const float> next(signal.data() + Dsp::kFftSize * 2,
                                    Dsp::kHopFrames * 2);

  Dsp dsp(model, rise_path, hop_path);
  Require(std::abs(dsp.WindowComplementRelativeError() -
                   metrics.window_complement_relative) < 1.0e-12,
          "production complement measurement mismatch");
  Dsp::HopStereo dry_first{};
  Dsp::HopStereo wet_first{};
  Dsp::HopStereo dry_second{};
  Dsp::HopStereo wet_second{};
  dsp.Process(first, true, true, 4096, 4096, 4096, dry_first, wet_first);
  const auto first_state = dsp.ModelStateSnapshot();
  dsp.Process(next, false, true, 4096, 4096, 4096, dry_second, wet_second);
  const auto second_state = dsp.ModelStateSnapshot();
  Require(MaxAbs(first_state) > 0.0 && MaxAbs(second_state) > 0.0,
          "model recurrent state stayed zero through DSP integration");

  for (std::size_t frame = 0; frame < Dsp::kHopFrames; ++frame) {
    for (std::size_t channel = 0; channel < 2; ++channel) {
      const std::size_t sample = frame * 2 + channel;
      Require(dry_first[sample] == signal[sample], "first dry timeline mismatch");
      const float window = frame < Dsp::kOverlapFrames
                               ? rise[frame]
                               : hop[frame - Dsp::kOverlapFrames];
      const double expected = static_cast<double>(signal[sample]) * window * window;
      metrics.production_first_transient_max_error =
          std::max(metrics.production_first_transient_max_error,
                   std::abs(static_cast<double>(wet_first[sample]) - expected));
      const std::size_t global_second = (Dsp::kHopFrames + frame) * 2 + channel;
      Require(dry_second[sample] == signal[global_second],
              "steady dry timeline/stereo order mismatch");
      metrics.production_steady_identity_max_error =
          std::max(metrics.production_steady_identity_max_error,
                   std::abs(static_cast<double>(wet_second[sample]) -
                            signal[global_second]));
    }
  }
  Require(metrics.production_first_transient_max_error < 8.0e-5,
          "production first-hop identity transient mismatch");
  Require(metrics.production_steady_identity_max_error < 8.0e-5,
          "production steady identity WOLA mismatch");

  // Non-unity integrated parity: independently recompute analysis spectra,
  // compare model heads/state against BuiltinRefOpResolver with the exact same
  // per-hop state, then independently remix, direct-IDFT and synthesize using
  // the production heads. This prevents the unity-gain identity case from
  // masking model-head/order/scaling errors.
  Dsp integrated(model, rise_path, hop_path);
  Dsp::HopStereo integrated_dry_first{};
  Dsp::HopStereo integrated_wet_first{};
  Dsp::HopStereo integrated_dry_second{};
  Dsp::HopStereo integrated_wet_second{};
  auto trace_first = std::make_unique<Dsp::HopTrace>();
  auto trace_second = std::make_unique<Dsp::HopTrace>();
  integrated.Process(first, true, true, 4096, 1295, 410,
                     integrated_dry_first, integrated_wet_first, trace_first.get());
  integrated.Process(next, false, true, 4096, 1295, 410,
                     integrated_dry_second, integrated_wet_second, trace_second.get());

  const std::span<const float> second_analysis_frame(
      signal.data() + Dsp::kHopFrames * 2, Dsp::kFftSize * 2);
  const auto direct_first_left =
      DirectSpectrumFromStereo(first, 0, rise, hop);
  const auto direct_first_right =
      DirectSpectrumFromStereo(first, 1, rise, hop);
  const auto direct_second_left =
      DirectSpectrumFromStereo(second_analysis_frame, 0, rise, hop);
  const auto direct_second_right =
      DirectSpectrumFromStereo(second_analysis_frame, 1, rise, hop);
  const auto direct_first_model_left = IndependentPackModelInput(direct_first_left);
  const auto direct_first_model_right = IndependentPackModelInput(direct_first_right);
  const auto direct_second_model_left = IndependentPackModelInput(direct_second_left);
  const auto direct_second_model_right = IndependentPackModelInput(direct_second_right);
  metrics.integrated_feature_max_abs = std::max(
      {MaxAbsDifference(trace_first->model_left, direct_first_model_left),
       MaxAbsDifference(trace_first->model_right, direct_first_model_right),
       MaxAbsDifference(trace_second->model_left, direct_second_model_left),
       MaxAbsDifference(trace_second->model_right, direct_second_model_right)});
  Require(metrics.integrated_feature_max_abs < 2.0e-4,
          "production model features differ from independent scalar DFT");

  ReferenceModel reference_model(model);
  const auto ref_first = reference_model.Run(
      trace_first->model_left, trace_first->model_right, trace_first->state_before);
  const auto ref_second = reference_model.Run(
      trace_second->model_left, trace_second->model_right, trace_second->state_before);
  const std::array<NumericMetrics, 10> model_checks = {
      CompareModelValues(trace_first->heads.voice_left, ref_first->voice_left),
      CompareModelValues(trace_first->heads.voice_right, ref_first->voice_right),
      CompareModelValues(trace_first->state_after, ref_first->state),
      CompareModelValues(trace_first->heads.music_left, ref_first->music_left),
      CompareModelValues(trace_first->heads.music_right, ref_first->music_right),
      CompareModelValues(trace_second->heads.voice_left, ref_second->voice_left),
      CompareModelValues(trace_second->heads.voice_right, ref_second->voice_right),
      CompareModelValues(trace_second->state_after, ref_second->state),
      CompareModelValues(trace_second->heads.music_left, ref_second->music_left),
      CompareModelValues(trace_second->heads.music_right, ref_second->music_right)};
  for (std::size_t i = 0; i < model_checks.size(); ++i) {
    Require(ModelParityPass(model_checks[i]),
            "integrated model head/state reference parity failed");
    if (i == 2 || i == 7) {
      metrics.integrated_model_state_max_abs =
          std::max(metrics.integrated_model_state_max_abs,
                   model_checks[i].max_abs);
    } else {
      metrics.integrated_model_head_max_abs =
          std::max(metrics.integrated_model_head_max_abs,
                   model_checks[i].max_abs);
    }
  }

  const auto scalar_remix_first_left =
      IndependentRemix(trace_first->original_left, trace_first->heads.voice_left,
                       trace_first->heads.music_left, 4096, 1295, 410);
  const auto scalar_remix_first_right =
      IndependentRemix(trace_first->original_right, trace_first->heads.voice_right,
                       trace_first->heads.music_right, 4096, 1295, 410);
  const auto scalar_remix_second_left =
      IndependentRemix(trace_second->original_left, trace_second->heads.voice_left,
                       trace_second->heads.music_left, 4096, 1295, 410);
  const auto scalar_remix_second_right =
      IndependentRemix(trace_second->original_right, trace_second->heads.voice_right,
                       trace_second->heads.music_right, 4096, 1295, 410);
  metrics.integrated_remix_max_abs = std::max(
      {SpectrumMaxAbsDifference(trace_first->remixed_left, scalar_remix_first_left),
       SpectrumMaxAbsDifference(trace_first->remixed_right, scalar_remix_first_right),
       SpectrumMaxAbsDifference(trace_second->remixed_left, scalar_remix_second_left),
       SpectrumMaxAbsDifference(trace_second->remixed_right, scalar_remix_second_right)});
  Require(metrics.integrated_remix_max_abs < 1.0e-7,
          "production float residual remix differs from scalar reference");

  Dsp::HopStereo scalar_tail{};
  const auto scalar_first = IndependentSynthesize(
      DirectInverse(scalar_remix_first_left),
      DirectInverse(scalar_remix_first_right), rise, hop, scalar_tail);
  const auto scalar_second = IndependentSynthesize(
      DirectInverse(scalar_remix_second_left),
      DirectInverse(scalar_remix_second_right), rise, hop, scalar_tail);
  metrics.integrated_final_wet_max_abs =
      std::max(MaxAbsDifference(integrated_wet_first, scalar_first),
               MaxAbsDifference(integrated_wet_second, scalar_second));
  Require(metrics.integrated_final_wet_max_abs < 8.0e-5,
          "production inverse/synthesis differs from direct scalar reference");

  dsp.ResetAll();
  Dsp::HopStereo replay_first_dry{};
  Dsp::HopStereo replay_first_wet{};
  Dsp::HopStereo replay_second_dry{};
  Dsp::HopStereo replay_second_wet{};
  dsp.Process(first, true, true, 4096, 4096, 4096, replay_first_dry,
              replay_first_wet);
  dsp.Process(next, false, true, 4096, 4096, 4096, replay_second_dry,
              replay_second_wet);
  metrics.reset_replay_max_error =
      std::max(MaxAbsDifference(wet_first, replay_first_wet),
               MaxAbsDifference(wet_second, replay_second_wet));
  metrics.reset_replay_state_max_error =
      MaxAbsDifference(second_state, dsp.ModelStateSnapshot());
  Require(metrics.reset_replay_max_error == 0.0 &&
              metrics.reset_replay_state_max_error == 0.0,
          "DSP resetAll replay was not deterministic");

  Dsp reset_neural(model, rise_path, hop_path);
  Dsp::HopStereo reset_first_dry{};
  Dsp::HopStereo reset_first_wet{};
  Dsp::HopStereo reset_second_dry{};
  Dsp::HopStereo reset_second_wet{};
  reset_neural.Process(first, true, true, 4096, 4096, 4096, reset_first_dry,
                       reset_first_wet);
  reset_neural.ResetNeuralState();
  reset_neural.Process(next, false, true, 4096, 4096, 4096,
                       reset_second_dry, reset_second_wet);
  Require(MaxAbsDifference(dry_second, reset_second_dry) == 0.0,
          "neural reset incorrectly cleared analysis history");
  metrics.recurrent_state_reset_delta =
      MaxAbsDifference(second_state, reset_neural.ModelStateSnapshot());
  Require(metrics.recurrent_state_reset_delta > 1.0e-6,
          "neural reset did not change recurrent trajectory");

  Dsp bypass(model, rise_path, hop_path);
  Dsp::HopStereo bypass_dry_first{};
  Dsp::HopStereo bypass_wet_first{};
  Dsp::HopStereo bypass_dry_second{};
  Dsp::HopStereo bypass_wet_second{};
  bypass.Process(first, true, false, 4096, 1295, 410, bypass_dry_first,
                 bypass_wet_first);
  bypass.Process(next, false, false, 4096, 1295, 410, bypass_dry_second,
                 bypass_wet_second);
  Require(MaxAbsDifference(bypass_dry_first, bypass_wet_first) == 0.0 &&
              MaxAbsDifference(bypass_dry_second, bypass_wet_second) == 0.0,
          "runModel=false did not produce exact wet=dry bypass");
  Require(MaxAbsDifference(bypass_dry_second, dry_second) == 0.0,
          "bypass did not advance analysis history on the same timeline");
}

void VerifyOfflineAndWav(const std::filesystem::path& model,
                         const std::filesystem::path& rise_path,
                         const std::filesystem::path& hop_path,
                         const std::filesystem::path& evidence,
                         VerificationMetrics& metrics) {
  cleanwave::OfflineCleaner bypass_cleaner(model, rise_path, hop_path);
  bypass_cleaner.SetReductionPercent(0);
  cleanwave::OfflineCleaner processed_boundary_cleaner(model, rise_path,
                                                              hop_path);
  cleanwave::OfflineCleaner manual_boundary_cleaner(model, rise_path,
                                                           hop_path);
  const std::array<std::size_t, 12> lengths = {
      1, 575, 576, 959, 960, 961, 1535, 1536, 1537, 1919, 1920, 1921};
  for (const std::size_t frames : lengths) {
    const auto input = MakeStereoPcm(frames);
    const auto output = cleanwave::ProcessOfflinePcm16(bypass_cleaner, input, false);
    Require(output.size() == input.size(), "finite-file output duration changed");
    Require(std::equal(output.begin(), output.end(), input.begin()),
            "finite-file dry/bypass PCM was not exact");

    const auto processed =
        cleanwave::ProcessOfflinePcm16(processed_boundary_cleaner, input, true);
    const auto manual =
        ManualFiniteProcessedReference(manual_boundary_cleaner, input);
    Require(processed.size() == input.size() && manual.size() == input.size(),
            "processed EOF reference changed output duration");
    for (std::size_t i = 0; i < processed.size(); ++i) {
      metrics.processed_eof_reference_max_pcm_delta =
          std::max(metrics.processed_eof_reference_max_pcm_delta,
                   static_cast<double>(std::abs(
                       static_cast<std::int32_t>(processed[i]) -
                       static_cast<std::int32_t>(manual[i]))));
    }
    Require(processed == manual,
            "finite-file processed zero-lookahead/tail reference mismatch");
  }

  cleanwave::WavPcm16Stereo fixture;
  fixture.samples = MakeStereoPcm(2813);
  const auto input_path = evidence / "fixture-stereo-partial.wav";
  const auto dry_path = evidence / "fixture-dry-exact.wav";
  const auto processed_path = evidence / "fixture-default-processed.wav";
  const auto odd_chunk_path = evidence / "fixture-odd-unknown-chunk.wav";
  const auto unicode_path =
      evidence / std::filesystem::path(L"fixture-\u0645\u0633\u0627\u0631.wav");
  cleanwave::WritePcm16StereoWav(input_path, fixture);
  const auto reread = cleanwave::ReadPcm16StereoWav(input_path);
  Require(reread.sample_rate == 48000 && reread.samples == fixture.samples,
          "PCM16 WAV write/read round-trip failed");
  const auto short_fixture = MakeStereoPcm(17);
  WriteOddUnknownChunkFixture(odd_chunk_path, short_fixture);
  Require(cleanwave::ReadPcm16StereoWav(odd_chunk_path).samples == short_fixture,
          "odd-sized unknown RIFF chunk was not parsed correctly");
  cleanwave::WavPcm16Stereo unicode_fixture;
  unicode_fixture.samples = short_fixture;
  cleanwave::WritePcm16StereoWav(unicode_path, unicode_fixture);
  Require(cleanwave::ReadPcm16StereoWav(unicode_path).samples == short_fixture,
          "Unicode Windows WAV path round-trip failed");

  cleanwave::OfflineCleaner dry_model_cleaner(model, rise_path, hop_path);
  dry_model_cleaner.SetReductionPercent(0);
  const auto dry_model_output =
      cleanwave::ProcessOfflinePcm16(dry_model_cleaner, fixture.samples, true);
  Require(dry_model_output == fixture.samples,
          "actual-model reduction=0 did not preserve exact original PCM");
  cleanwave::WavPcm16Stereo dry_wav;
  dry_wav.samples = dry_model_output;
  cleanwave::WritePcm16StereoWav(dry_path, dry_wav);

  cleanwave::OfflineCleaner processed_cleaner(model, rise_path, hop_path);
  const auto processed =
      cleanwave::ProcessOfflinePcm16(processed_cleaner, fixture.samples, true);
  Require(processed.size() == fixture.samples.size(),
          "actual-model processed WAV changed frame count");
  bool differs = false;
  for (std::size_t i = 0; i < processed.size(); ++i) {
    const std::int32_t delta = std::abs(static_cast<std::int32_t>(processed[i]) -
                                        static_cast<std::int32_t>(fixture.samples[i]));
    metrics.actual_model_max_pcm_delta =
        std::max(metrics.actual_model_max_pcm_delta, delta);
    differs = differs || delta != 0;
  }
  Require(differs, "default actual-model processing was identical to dry input");
  cleanwave::WavPcm16Stereo processed_wav;
  processed_wav.samples = processed;
  cleanwave::WritePcm16StereoWav(processed_path, processed_wav);
  const auto processed_reread = cleanwave::ReadPcm16StereoWav(processed_path);
  Require(processed_reread.samples == processed,
          "processed WAV could not be read back exactly");

  // Reusing the same cleaner for independent finite files must not carry
  // recurrent/history/synthesis or AudioLevel adaptation across streams.
  const auto processed_again =
      cleanwave::ProcessOfflinePcm16(processed_cleaner, fixture.samples, true);
  Require(processed_again == processed,
          "reused offline cleaner leaked state across independent files");
}

int Run(const std::filesystem::path& model,
        const std::filesystem::path& rise_path,
        const std::filesystem::path& hop_path,
        const std::filesystem::path& evidence) {
  std::filesystem::create_directories(evidence);
  VerificationMetrics metrics;

  VerifyWindowsAndFft(rise_path, hop_path, metrics);
  VerifyPackAndRemix();
  VerifyAudioLevel();
  VerifyProductionDsp(model, rise_path, hop_path, metrics);
  VerifyOfflineAndWav(model, rise_path, hop_path, evidence, metrics);

  std::ostringstream report;
  report << std::setprecision(12);
  report << "CleanWave Step 4 DSP/offline verification\n"
         << "sample_rate=48000 fft=1536 hop=960 overlap=576 direct=384\n"
         << "finite_file_policy=Windows-only zero future context; exact-duration trim\n\n"
         << "Window/FFT references\n"
         << "  q15_decode_max_abs=" << metrics.window_decode_max << "\n"
         << "  window_complement_worst_relative="
         << metrics.window_complement_relative << "\n"
         << "  kissfft_vs_scalar_dft_max_component_error="
         << metrics.dft_max_complex_error << "\n"
         << "  fft_inverse_roundtrip_max_abs=" << metrics.fft_roundtrip_max_error
         << "\n"
         << "  android_style_steady_wola_max_abs=" << metrics.steady_wola_max_error
         << "\n\n"
         << "Production DSP identity/reset\n"
         << "  first_hop_transient_max_abs="
         << metrics.production_first_transient_max_error << "\n"
         << "  steady_identity_max_abs="
         << metrics.production_steady_identity_max_error << "\n"
         << "  integrated_feature_max_abs=" << metrics.integrated_feature_max_abs
         << "\n"
         << "  integrated_model_head_max_abs="
         << metrics.integrated_model_head_max_abs << "\n"
         << "  integrated_model_state_max_abs="
         << metrics.integrated_model_state_max_abs << "\n"
         << "  integrated_scalar_remix_max_abs="
         << metrics.integrated_remix_max_abs << "\n"
         << "  integrated_direct_idft_synthesis_max_abs="
         << metrics.integrated_final_wet_max_abs << "\n"
         << "  reset_replay_output_max_abs=" << metrics.reset_replay_max_error
         << "\n"
         << "  reset_replay_state_max_abs=" << metrics.reset_replay_state_max_error
         << "\n"
         << "  continuous_vs_neural_reset_state_max_abs="
         << metrics.recurrent_state_reset_delta << "\n\n"
         << "Offline PCM/WAV\n"
         << "  eof_boundary_cases=12 PASS\n"
         << "  processed_eof_reference_max_pcm_delta="
         << metrics.processed_eof_reference_max_pcm_delta << "\n"
         << "  reduction0_actual_model_pcm_exact=PASS\n"
         << "  reused_cleaner_independent_stream_replay=PASS\n"
         << "  odd_unknown_riff_chunk=PASS\n"
         << "  unicode_wav_path=PASS\n"
         << "  default_processed_frame_count=2813\n"
         << "  default_processed_max_pcm_delta=" << metrics.actual_model_max_pcm_delta
         << "\n"
         << "  audiolevel_reduction0_ramp_compensation_boost_limiter_nonfinite=PASS\n"
         << "  pack_scale_upper_band_residual_q12=PASS\n"
         << "RESULT=PASS\n";

  const std::string text = report.str();
  std::ofstream out(evidence / "dsp-verification.txt", std::ios::trunc);
  if (!out) throw std::runtime_error("cannot create dsp-verification.txt");
  out << text;
  std::cout << text;
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc != 5) {
      std::cerr << "Usage: cleanwave_dsp_verify <model.tflite> <rise.bin> <hop.bin> "
                   "<evidence-directory>\n";
      return 2;
    }
    return Run(argv[1], argv[2], argv[3], argv[4]);
  } catch (const std::exception& error) {
    std::cerr << "Step 4 DSP verification failed: " << error.what() << '\n';
    return 1;
  }
}
