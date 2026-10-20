#include "dsp/StreamingDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "kiss_fftr.h"

namespace cleanwave {
namespace {

constexpr float kQ15 = 32768.0F;
constexpr float kModelInputScale = 32768.0F / 32767.0F;
constexpr float kModelOutputScale = 32767.0F / 32768.0F;

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error("unable to open Audio window asset");
  const std::streamoff size = stream.tellg();
  if (size < 0) throw std::runtime_error("unable to size Audio window asset");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream) throw std::runtime_error("unable to read Audio window asset");
  return bytes;
}

}  // namespace

class StreamingDsp::Impl {
 public:
  Impl(const std::filesystem::path& model_path,
       const std::filesystem::path& rise_window_path,
       const std::filesystem::path& hop_window_path)
      : rise_(DecodeQ15Window(rise_window_path, kOverlapFrames)),
        hop_window_(DecodeQ15Window(hop_window_path, kHopFrames)),
        model_(model_path) {
    forward_ = kiss_fftr_alloc(static_cast<int>(kFftSize), 0, nullptr, nullptr);
    inverse_ = kiss_fftr_alloc(static_cast<int>(kFftSize), 1, nullptr, nullptr);
    if (forward_ == nullptr || inverse_ == nullptr) {
      CleanupFft();
      throw std::runtime_error("unable to create 1536-point KissFFT plans");
    }

    const double target = static_cast<double>(32767LL * 32767LL);
    complement_error_ = 0.0;
    for (std::size_t i = 0; i < kOverlapFrames; ++i) {
      const auto a = static_cast<long long>(rise_[i] * kQ15);
      const auto b = static_cast<long long>(hop_window_[kDirectFrames + i] * kQ15);
      const double error =
          std::abs(static_cast<double>(a * a + b * b) - target) / target;
      complement_error_ = std::max(complement_error_, error);
    }
    if (complement_error_ >= 0.0002) {
      CleanupFft();
      throw std::runtime_error("Audio Q15 windows are not complementary");
    }
  }

  ~Impl() { CleanupFft(); }

  void Process(std::span<const float> input, bool first, bool run_model,
               int voice_q12, int music_q12, int other_q12, HopStereo& dry,
               HopStereo& wet, HopTrace* trace) {
    const std::size_t expected_frames = first ? kFirstInputFrames : kHopFrames;
    if (input.size() != expected_frames * 2) {
      throw std::runtime_error("unexpected Audio DSP input size");
    }

    if (first) {
      std::copy(input.begin(), input.end(), frame_.begin());
    } else {
      std::copy(history_.begin(), history_.end(), frame_.begin());
      std::copy(input.begin(), input.end(), frame_.begin() + history_.size());
    }

    std::copy_n(frame_.begin(), dry.size(), dry.begin());
    std::copy(frame_.begin() + dry.size(), frame_.end(), history_.begin());

    if (!run_model) {
      wet = dry;
      if (trace != nullptr) {
        *trace = HopTrace{};
        trace->state_before = model_.StateSnapshot();
        trace->state_after = trace->state_before;
      }
      return;
    }

    const Spectrum left_spectrum = AnalyzeChannel(0, left_time_, left_raw_);
    const Spectrum right_spectrum = AnalyzeChannel(1, right_time_, right_raw_);
    const auto model_left = PackModelInput(left_spectrum);
    const auto model_right = PackModelInput(right_spectrum);

    SeparationModel::SourceHeads heads;
    if (trace != nullptr) trace->state_before = model_.StateSnapshot();
    model_.Process(model_left, model_right, heads);
    if (trace != nullptr) trace->state_after = model_.StateSnapshot();

    const Spectrum wet_left = RemixSpectrum(left_spectrum, heads.voice_left,
                                            heads.music_left, voice_q12,
                                            music_q12, other_q12);
    const Spectrum wet_right = RemixSpectrum(right_spectrum, heads.voice_right,
                                             heads.music_right, voice_q12,
                                             music_q12, other_q12);

    Inverse(wet_left, left_inverse_);
    Inverse(wet_right, right_inverse_);
    Synthesize(left_inverse_, right_inverse_, wet);

    if (trace != nullptr) {
      trace->original_left = left_spectrum;
      trace->original_right = right_spectrum;
      trace->remixed_left = wet_left;
      trace->remixed_right = wet_right;
      trace->model_left = model_left;
      trace->model_right = model_right;
      trace->heads = heads;
    }
  }

  void ResetNeuralState() {
    model_.Reset();
    synthesis_tail_.fill(0.0F);
  }

  void ResetAll() {
    history_.fill(0.0F);
    ResetNeuralState();
  }

  [[nodiscard]] SeparationModel::StateBuffer StateSnapshot() const {
    return model_.StateSnapshot();
  }

  [[nodiscard]] double ComplementError() const { return complement_error_; }

 private:
  Spectrum AnalyzeChannel(std::size_t channel,
                          std::array<float, kFftSize>& time,
                          std::array<kiss_fft_cpx, kSpectrumBins>& raw) {
    for (std::size_t i = 0; i < kFftSize; ++i) {
      const float window =
          i < kOverlapFrames ? rise_[i] : hop_window_[i - kOverlapFrames];
      time[i] = frame_[i * 2 + channel] * window;
    }
    kiss_fftr(forward_, time.data(), raw.data());
    Spectrum result{};
    for (std::size_t bin = 0; bin < kSpectrumBins; ++bin) {
      result[bin].real = raw[bin].r;
      result[bin].imag = raw[bin].i;
    }
    return result;
  }

  void Inverse(const Spectrum& spectrum, std::array<float, kFftSize>& time) {
    for (std::size_t bin = 0; bin < kSpectrumBins; ++bin) {
      inverse_raw_[bin].r = spectrum[bin].real;
      inverse_raw_[bin].i = spectrum[bin].imag;
    }
    kiss_fftri(inverse_, inverse_raw_.data(), time.data());
    constexpr float inverse_scale = 1.0F / static_cast<float>(kFftSize);
    for (float& sample : time) sample *= inverse_scale;
  }

  void Synthesize(const std::array<float, kFftSize>& left,
                  const std::array<float, kFftSize>& right, HopStereo& output) {
    for (std::size_t i = 0; i < kOverlapFrames; ++i) {
      const float falling = hop_window_[kDirectFrames + i];
      const float rising = rise_[i];
      output[i * 2] = synthesis_tail_[i * 2] * falling + left[i] * rising;
      output[i * 2 + 1] =
          synthesis_tail_[i * 2 + 1] * falling + right[i] * rising;
    }
    for (std::size_t i = kOverlapFrames; i < kHopFrames; ++i) {
      const float window = hop_window_[i - kOverlapFrames];
      output[i * 2] = left[i] * window;
      output[i * 2 + 1] = right[i] * window;
    }
    for (std::size_t i = 0; i < kOverlapFrames; ++i) {
      synthesis_tail_[i * 2] = left[kHopFrames + i];
      synthesis_tail_[i * 2 + 1] = right[kHopFrames + i];
    }
  }

  void CleanupFft() noexcept {
    if (forward_ != nullptr) kiss_fftr_free(forward_);
    if (inverse_ != nullptr) kiss_fftr_free(inverse_);
    forward_ = nullptr;
    inverse_ = nullptr;
  }

  std::vector<float> rise_;
  std::vector<float> hop_window_;
  SeparationModel model_;
  kiss_fftr_cfg forward_ = nullptr;
  kiss_fftr_cfg inverse_ = nullptr;
  double complement_error_ = 0.0;

  std::array<float, kOverlapFrames * 2> history_{};
  std::array<float, kFftSize * 2> frame_{};
  std::array<float, kFftSize> left_time_{};
  std::array<float, kFftSize> right_time_{};
  std::array<float, kFftSize> left_inverse_{};
  std::array<float, kFftSize> right_inverse_{};
  std::array<kiss_fft_cpx, kSpectrumBins> left_raw_{};
  std::array<kiss_fft_cpx, kSpectrumBins> right_raw_{};
  std::array<kiss_fft_cpx, kSpectrumBins> inverse_raw_{};
  HopStereo synthesis_tail_{};
};

StreamingDsp::StreamingDsp(
    const std::filesystem::path& model_path,
    const std::filesystem::path& rise_window_path,
    const std::filesystem::path& hop_window_path)
    : impl_(std::make_unique<Impl>(model_path, rise_window_path,
                                   hop_window_path)) {}

StreamingDsp::~StreamingDsp() = default;
StreamingDsp::StreamingDsp(StreamingDsp&&) noexcept = default;
StreamingDsp& StreamingDsp::operator=(StreamingDsp&&) noexcept = default;

void StreamingDsp::Process(std::span<const float> interleaved_stereo,
                                  bool first, bool run_model, int voice_q12,
                                  int music_q12, int other_q12,
                                  HopStereo& dry_output,
                                  HopStereo& wet_output, HopTrace* trace) {
  impl_->Process(interleaved_stereo, first, run_model, voice_q12, music_q12,
                 other_q12, dry_output, wet_output, trace);
}

void StreamingDsp::ResetNeuralState() { impl_->ResetNeuralState(); }
void StreamingDsp::ResetAll() { impl_->ResetAll(); }

SeparationModel::StateBuffer StreamingDsp::ModelStateSnapshot() const {
  return impl_->StateSnapshot();
}

double StreamingDsp::WindowComplementRelativeError() const {
  return impl_->ComplementError();
}

std::vector<float> StreamingDsp::DecodeQ15Window(
    const std::filesystem::path& path, std::size_t expected_samples) {
  const auto bytes = ReadBytes(path);
  if (bytes.size() != expected_samples * 2) {
    throw std::runtime_error("unexpected Audio Q15 window size");
  }
  std::vector<float> result(expected_samples);
  for (std::size_t i = 0; i < expected_samples; ++i) {
    const std::uint16_t packed = static_cast<std::uint16_t>(bytes[i * 2]) |
                                 (static_cast<std::uint16_t>(bytes[i * 2 + 1])
                                  << 8U);
    const auto signed_value = static_cast<std::int16_t>(packed);
    result[i] = static_cast<float>(signed_value) / kQ15;
  }
  return result;
}

SeparationModel::SpectralBuffer StreamingDsp::PackModelInput(
    const Spectrum& spectrum) {
  SeparationModel::SpectralBuffer result{};
  for (std::size_t bin = 0; bin < kModelBins; ++bin) {
    result[bin * 2] = spectrum[bin].real * kModelInputScale;
    result[bin * 2 + 1] = spectrum[bin].imag * kModelInputScale;
  }
  return result;
}

StreamingDsp::Spectrum StreamingDsp::RemixSpectrum(
    const Spectrum& original, const SeparationModel::SpectralBuffer& voice,
    const SeparationModel::SpectralBuffer& music, int voice_q12,
    int music_q12, int other_q12) {
  Spectrum destination{};
  const float gv = static_cast<float>(voice_q12) / static_cast<float>(kQ12One);
  const float gm = static_cast<float>(music_q12) / static_cast<float>(kQ12One);
  const float go = static_cast<float>(other_q12) / static_cast<float>(kQ12One);
  for (std::size_t bin = 0; bin < kSpectrumBins; ++bin) {
    const float voice_real =
        bin < kModelBins ? voice[bin * 2] * kModelOutputScale : 0.0F;
    const float voice_imag =
        bin < kModelBins ? voice[bin * 2 + 1] * kModelOutputScale : 0.0F;
    const float music_real =
        bin < kModelBins ? music[bin * 2] * kModelOutputScale : 0.0F;
    const float music_imag =
        bin < kModelBins ? music[bin * 2 + 1] * kModelOutputScale : 0.0F;
    destination[bin].real =
        voice_real * gv + music_real * gm +
        (original[bin].real - voice_real - music_real) * go;
    const float imag = voice_imag * gv + music_imag * gm +
                       (original[bin].imag - voice_imag - music_imag) * go;
    destination[bin].imag =
        (bin == 0 || bin == kFftSize / 2) ? 0.0F : imag;
  }
  return destination;
}

}  // namespace cleanwave
