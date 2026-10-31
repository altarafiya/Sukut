#include "model/SeparationModel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "model/AssetSeal.h"
#include "tensorflow/lite/c/c_api.h"
#if defined(TFLITE_BUILD_WITH_XNNPACK_DELEGATE)
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model_builder.h"
#define CLEANWAVE_SELECTABLE_KERNELS 1
#endif

namespace cleanwave {
namespace {

std::string ShapeString(const TfLiteTensor* tensor) {
  std::string result = "[";
  const int dims = TfLiteTensorNumDims(tensor);
  for (int i = 0; i < dims; ++i) {
    if (i != 0) result += ',';
    result += std::to_string(TfLiteTensorDim(tensor, i));
  }
  result += ']';
  return result;
}

void RequireTensor(const TfLiteTensor* tensor, const std::vector<int>& shape,
                   std::size_t expected_bytes, const std::string& label) {
  if (tensor == nullptr) {
    throw std::runtime_error(label + " tensor is null");
  }
  if (TfLiteTensorType(tensor) != kTfLiteFloat32) {
    throw std::runtime_error(label + " must be FLOAT32");
  }
  if (TfLiteTensorNumDims(tensor) != static_cast<int>(shape.size())) {
    throw std::runtime_error(label + " has unexpected shape " + ShapeString(tensor));
  }
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (TfLiteTensorDim(tensor, static_cast<int>(i)) != shape[i]) {
      throw std::runtime_error(label + " has unexpected shape " + ShapeString(tensor));
    }
  }
  if (TfLiteTensorByteSize(tensor) != expected_bytes) {
    throw std::runtime_error(label + " has unexpected byte size");
  }
}

void RequireOk(TfLiteStatus status, const char* operation) {
  if (status != kTfLiteOk) {
    throw std::runtime_error(std::string(operation) + " failed");
  }
}

// TFLite borrows these immutable bytes until after interpreter/model teardown.
// The sealed file is read straight into one private buffer and opened in
// place, so there is a single copy. No tensor/model data or arithmetic changes.
class ModelBytes final {
 public:
  explicit ModelBytes(const std::filesystem::path& path)
      : bytes_(seal::ReadFile(path, "Sukut model file")) {
    if(bytes_.size()<8 || std::string(reinterpret_cast<const char*>(bytes_.data())+4,4)!="TFL3")
      throw std::runtime_error("Sukut model file is not a Sukut model");
  }
  ModelBytes(const ModelBytes&)=delete;
  ModelBytes& operator=(const ModelBytes&)=delete;
  const std::uint8_t* data()const{return bytes_.data();}
  std::size_t size()const{return bytes_.size();}
 private:
  std::vector<std::uint8_t> bytes_;
};
}  // namespace

#if defined(CLEANWAVE_SELECTABLE_KERNELS)
// XNNPACK-capable build. The reference interpreter uses the built-in kernels
// with no default delegates (exactly the baseline path). XNNPACK is kept only
// if, on this CPU at start-up, it reproduces the reference heads and
// recurrent state within 1e-3 relative (-60 dB) over a recurrent run AND is
// at least 15% faster. Otherwise, or on any error, the reference stays.
// SUKUT_KERNELS=reference forces the reference path.
class SeparationModel::Impl {
  struct Runner {
    std::unique_ptr<tflite::Interpreter> interpreter;
    TfLiteDelegate* delegate = nullptr;
    std::array<StateBuffer, 2> states{};
    std::size_t index = 0;
    ~Runner() { interpreter.reset(); if (delegate) TfLiteXNNPackDelegateDelete(delegate); }
    void Reset() { states[0].fill(0.0F); states[1].fill(0.0F); index = 0; }
    void Run(const SpectralBuffer& left, const SpectralBuffer& right, SourceHeads& out) {
      std::memcpy(interpreter->typed_input_tensor<float>(0), left.data(), sizeof(left));
      std::memcpy(interpreter->typed_input_tensor<float>(1), right.data(), sizeof(right));
      std::memcpy(interpreter->typed_input_tensor<float>(2), states[index].data(), sizeof(StateBuffer));
      RequireOk(interpreter->Invoke(), "TFLite Invoke");
      std::memcpy(out.voice_left.data(), interpreter->typed_output_tensor<float>(0), sizeof(out.voice_left));
      std::memcpy(out.voice_right.data(), interpreter->typed_output_tensor<float>(1), sizeof(out.voice_right));
      std::memcpy(states[index ^ 1].data(), interpreter->typed_output_tensor<float>(2), sizeof(StateBuffer));
      std::memcpy(out.music_left.data(), interpreter->typed_output_tensor<float>(3), sizeof(out.music_left));
      std::memcpy(out.music_right.data(), interpreter->typed_output_tensor<float>(4), sizeof(out.music_right));
      index ^= 1;
    }
  };

 public:
  explicit Impl(const std::filesystem::path& model_path) : model_bytes_(model_path) {
    model_ = tflite::FlatBufferModel::BuildFromBuffer(
        reinterpret_cast<const char*>(model_bytes_.data()), model_bytes_.size());
    if (!model_) throw std::runtime_error("TFLite model buffer is invalid");
    auto reference = Build(false);
    std::unique_ptr<Runner> fast;
    const char* forced = std::getenv("SUKUT_KERNELS");
    if (!(forced && std::string(forced) == "reference")) {
      try { fast = Build(true); } catch (const std::exception&) { report_ = "builtin; xnnpack=unavailable"; }
    } else report_ = "builtin; forced";
    if (fast) {
      try { Compare(*reference, *fast); }
      catch (const std::exception&) { fast.reset(); report_ = "builtin; xnnpack=failed"; }
    }
    if (fast && worst_ <= 1e-3F && fast_ms_ < reference_ms_ * 0.85) {
      runner_ = std::move(fast); report_ = "xnnpack";
    } else {
      if (fast) report_ = "builtin; xnnpack=rejected";
      runner_ = std::move(reference);
    }
    runner_->Reset();
  }

  void Reset() { runner_->Reset(); }
  void Process(const SpectralBuffer& left, const SpectralBuffer& right, SourceHeads& outputs) {
    runner_->Run(left, right, outputs);
  }
  [[nodiscard]] StateBuffer StateSnapshot() const { return runner_->states[runner_->index]; }
  [[nodiscard]] std::string KernelReport() const {
    return report_ + "; reference_ms=" + std::to_string(reference_ms_) + "; selected_ms=" +
           std::to_string(report_ == "xnnpack" ? fast_ms_ : reference_ms_) + "; max_relative_error=" + std::to_string(worst_) +
           "; heads_error=" + std::to_string(heads_error_) + "; state_error=" + std::to_string(state_error_) +
           "; xnnpack_ms=" + std::to_string(fast_ms_);
  }

 private:
  std::unique_ptr<Runner> Build(bool xnnpack) {
    auto runner = std::make_unique<Runner>();
    tflite::ops::builtin::BuiltinOpResolverWithoutDefaultDelegates resolver;
    tflite::InterpreterBuilder builder(*model_, resolver);
    builder.SetNumThreads(1);
    if (builder(&runner->interpreter) != kTfLiteOk || !runner->interpreter)
      throw std::runtime_error("TFLite InterpreterBuilder failed");
    if (xnnpack) {
      TfLiteXNNPackDelegateOptions options = TfLiteXNNPackDelegateOptionsDefault();
      options.num_threads = 1;
      runner->delegate = TfLiteXNNPackDelegateCreate(&options);
      if (!runner->delegate) throw std::runtime_error("XNNPACK delegate unavailable");
      RequireOk(runner->interpreter->ModifyGraphWithDelegate(runner->delegate), "XNNPACK ModifyGraphWithDelegate");
    }
    RequireOk(runner->interpreter->AllocateTensors(), "AllocateTensors");
    Validate(*runner->interpreter);
    return runner;
  }

  static void Validate(tflite::Interpreter& interpreter) {
    if (interpreter.inputs().size() != 3 || interpreter.outputs().size() != kOutputCount)
      throw std::runtime_error("Sukut model has an unexpected tensor contract");
    constexpr std::size_t spectral_bytes = kSpectralFloats * sizeof(float);
    constexpr std::size_t state_bytes = kStateFloats * sizeof(float);
    const std::vector<int> spectral_shape{1, 1, 1024}, state_shape{1, 6, 52, 48};
    RequireTensor(interpreter.input_tensor(0), spectral_shape, spectral_bytes, "input 0");
    RequireTensor(interpreter.input_tensor(1), spectral_shape, spectral_bytes, "input 1");
    RequireTensor(interpreter.input_tensor(2), state_shape, state_bytes, "input 2");
    for (std::size_t o = 0; o < kOutputCount; ++o)
      RequireTensor(interpreter.output_tensor(o), o == 2 ? state_shape : spectral_shape,
                    o == 2 ? state_bytes : spectral_bytes, "output " + std::to_string(o));
  }

  static float RelativeError(const float* expected, const float* actual, std::size_t count) {
    float peak = 0.0F, difference = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
      peak = std::max(peak, std::abs(expected[i]));
      const float d = std::abs(expected[i] - actual[i]);
      if (!std::isfinite(d)) return INFINITY;
      difference = std::max(difference, d);
    }
    return peak < 1e-6F ? difference : difference / peak;
  }

  void Compare(Runner& reference, Runner& fast) {
    constexpr int kWarmup = 8, kMeasured = 40;
    SpectralBuffer left{}, right{};
    SourceHeads a{}, b{};
    std::uint64_t seed = 0x53554b55ULL;
    const auto next = [&] {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      return static_cast<float>(static_cast<std::int32_t>(seed >> 40)) / 8388608.0F - 1.0F;
    };
    double reference_ns = 0, fast_ns = 0;
    reference.Reset(); fast.Reset();
    for (int frame = 0; frame < kWarmup + kMeasured; ++frame) {
      const float scale = std::array<float, 4>{.01F, 1.0F, 30.0F, 200.0F}[(frame / 6) % 4];
      for (std::size_t i = 0; i < kSpectralFloats; ++i) {
        left[i] = frame % 11 == 0 ? 0.0F : next() * scale;
        right[i] = frame % 13 == 0 ? 0.0F : next() * scale;
      }
      auto start = std::chrono::steady_clock::now();
      reference.Run(left, right, a);
      const auto r = std::chrono::steady_clock::now() - start;
      start = std::chrono::steady_clock::now();
      fast.Run(left, right, b);
      const auto f = std::chrono::steady_clock::now() - start;
      if (frame >= kWarmup) {
        reference_ns += std::chrono::duration<double, std::nano>(r).count();
        fast_ns += std::chrono::duration<double, std::nano>(f).count();
      }
      worst_ = std::max({worst_, RelativeError(a.voice_left.data(), b.voice_left.data(), kSpectralFloats),
                         RelativeError(a.voice_right.data(), b.voice_right.data(), kSpectralFloats),
                         RelativeError(a.music_left.data(), b.music_left.data(), kSpectralFloats),
                         RelativeError(a.music_right.data(), b.music_right.data(), kSpectralFloats)});
    }
    heads_error_ = worst_;
    state_error_ = RelativeError(reference.states[reference.index].data(), fast.states[fast.index].data(), kStateFloats);
    worst_ = std::max(worst_, state_error_);
    reference_ms_ = reference_ns / 1e6 / kMeasured;
    fast_ms_ = fast_ns / 1e6 / kMeasured;
  }

  ModelBytes model_bytes_;
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<Runner> runner_;
  std::string report_ = "builtin";
  float worst_ = 0.0F, heads_error_ = 0.0F, state_error_ = 0.0F;
  double reference_ms_ = 0, fast_ms_ = 0;
};
#else
class SeparationModel::Impl {
 public:
  explicit Impl(const std::filesystem::path& model_path) : model_bytes_(model_path) {
    try {
#if defined(CLEANWAVE_AVX2_RUNTIME)
      if (!IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE))
        throw std::runtime_error("this runtime needs AVX2; rebuild with CLEANWAVE_CPU_RUNTIME=Baseline on this CPU");
#elif defined(CLEANWAVE_AVX_RUNTIME)
      if (!IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE))
        throw std::runtime_error("this runtime needs AVX; rebuild with CLEANWAVE_CPU_RUNTIME=Baseline on this CPU");
#endif
      model_ = TfLiteModelCreate(model_bytes_.data(), model_bytes_.size());
      if (model_ == nullptr) {
        throw std::runtime_error("TfLiteModelCreate failed for Sukut model buffer");
      }

      TfLiteInterpreterOptions* options = TfLiteInterpreterOptionsCreate();
      if (options == nullptr) {
        throw std::runtime_error("TfLiteInterpreterOptionsCreate failed");
      }
      TfLiteInterpreterOptionsSetNumThreads(options, 1);
      interpreter_ = TfLiteInterpreterCreate(model_, options);
      TfLiteInterpreterOptionsDelete(options);
      if (interpreter_ == nullptr) {
        throw std::runtime_error("TfLiteInterpreterCreate failed");
      }

      RequireOk(TfLiteInterpreterAllocateTensors(interpreter_),
                "TfLiteInterpreterAllocateTensors");
      ValidateContract();
      Reset();
    } catch (...) {
      Cleanup();
      throw;
    }
  }

  ~Impl() { Cleanup(); }

  void Cleanup() noexcept {
    if (interpreter_ != nullptr) TfLiteInterpreterDelete(interpreter_);
    if (model_ != nullptr) TfLiteModelDelete(model_);
    interpreter_ = nullptr;
    model_ = nullptr;
  }

  void Reset() {
    states_[0].fill(0.0F);
    states_[1].fill(0.0F);
    state_index_ = 0;
  }

  void Process(const SpectralBuffer& left, const SpectralBuffer& right,
               SourceHeads& outputs) {
    RequireOk(TfLiteTensorCopyFromBuffer(
                  TfLiteInterpreterGetInputTensor(interpreter_, 0), left.data(),
                  sizeof(left)),
              "copy left input");
    RequireOk(TfLiteTensorCopyFromBuffer(
                  TfLiteInterpreterGetInputTensor(interpreter_, 1), right.data(),
                  sizeof(right)),
              "copy right input");
    RequireOk(TfLiteTensorCopyFromBuffer(
                  TfLiteInterpreterGetInputTensor(interpreter_, 2),
                  states_[state_index_].data(), sizeof(StateBuffer)),
              "copy recurrent state input");

    RequireOk(TfLiteInterpreterInvoke(interpreter_), "TfLiteInterpreterInvoke");

    CopyOutput(0, outputs.voice_left.data(), sizeof(outputs.voice_left));
    CopyOutput(1, outputs.voice_right.data(), sizeof(outputs.voice_right));
    CopyOutput(2, states_[state_index_ ^ 1].data(), sizeof(StateBuffer));
    CopyOutput(3, outputs.music_left.data(), sizeof(outputs.music_left));
    CopyOutput(4, outputs.music_right.data(), sizeof(outputs.music_right));
    state_index_ ^= 1;
  }

  [[nodiscard]] StateBuffer StateSnapshot() const { return states_[state_index_]; }
  [[nodiscard]] std::string KernelReport() const { return "builtin"; }

 private:
  void CopyOutput(int output_index, void* destination, std::size_t bytes) {
    RequireOk(TfLiteTensorCopyToBuffer(
                  TfLiteInterpreterGetOutputTensor(interpreter_, output_index),
                  destination, bytes),
              "copy model output");
  }

  void ValidateContract() {
    if (TfLiteInterpreterGetInputTensorCount(interpreter_) != 3) {
      throw std::runtime_error("Sukut model must expose exactly 3 inputs");
    }
    if (TfLiteInterpreterGetOutputTensorCount(interpreter_) !=
        static_cast<int>(kOutputCount)) {
      throw std::runtime_error("Sukut model must expose exactly 7 outputs");
    }

    constexpr std::size_t spectral_bytes = kSpectralFloats * sizeof(float);
    constexpr std::size_t state_bytes = kStateFloats * sizeof(float);
    const std::vector<int> spectral_shape{1, 1, 1024};
    const std::vector<int> state_shape{1, 6, 52, 48};

    RequireTensor(TfLiteInterpreterGetInputTensor(interpreter_, 0), spectral_shape,
                  spectral_bytes, "input 0");
    RequireTensor(TfLiteInterpreterGetInputTensor(interpreter_, 1), spectral_shape,
                  spectral_bytes, "input 1");
    RequireTensor(TfLiteInterpreterGetInputTensor(interpreter_, 2), state_shape,
                  state_bytes, "input 2");
    for (int output = 0; output < static_cast<int>(kOutputCount); ++output) {
      if (output == 2) {
        RequireTensor(TfLiteInterpreterGetOutputTensor(interpreter_, output),
                      state_shape, state_bytes, "output 2");
      } else {
        RequireTensor(TfLiteInterpreterGetOutputTensor(interpreter_, output),
                      spectral_shape, spectral_bytes,
                      "output " + std::to_string(output));
      }
    }
  }

  ModelBytes model_bytes_;
  TfLiteModel* model_ = nullptr;
  TfLiteInterpreter* interpreter_ = nullptr;
  std::array<StateBuffer, 2> states_{};
  std::size_t state_index_ = 0;
};
#endif

SeparationModel::SeparationModel(const std::filesystem::path& model_path)
    : impl_(std::make_unique<Impl>(model_path)) {}

SeparationModel::~SeparationModel() = default;
SeparationModel::SeparationModel(SeparationModel&&) noexcept = default;
SeparationModel& SeparationModel::operator=(SeparationModel&&) noexcept = default;

void SeparationModel::Reset() { impl_->Reset(); }

void SeparationModel::Process(const SpectralBuffer& left,
                                 const SpectralBuffer& right,
                                 SourceHeads& outputs) {
  impl_->Process(left, right, outputs);
}

SeparationModel::StateBuffer SeparationModel::StateSnapshot() const {
  return impl_->StateSnapshot();
}

std::string SeparationModel::KernelReport() const { return impl_->KernelReport(); }

}  // namespace cleanwave

