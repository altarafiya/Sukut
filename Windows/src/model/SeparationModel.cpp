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

#include "tensorflow/lite/c/c_api.h"

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
// A read-only file mapping keeps weights file-backed instead of retaining a
// second private heap copy. No tensor/model data or arithmetic changes.
class ModelBytes final {
 public:
  explicit ModelBytes(const std::filesystem::path& path) {
#if defined(_WIN32)
    try {
      file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(file_==INVALID_HANDLE_VALUE)throw std::runtime_error("unable to open Audio model file");
      LARGE_INTEGER length{};
      if(!GetFileSizeEx(file_,&length) || length.QuadPart<=0)throw std::runtime_error("Audio model file is empty or unreadable");
      size_=static_cast<std::size_t>(length.QuadPart);
      mapping_=CreateFileMappingW(file_,nullptr,PAGE_READONLY,0,0,nullptr);
      if(!mapping_)throw std::runtime_error("unable to map Audio model file");
      data_=static_cast<const std::uint8_t*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0));
      if(!data_)throw std::runtime_error("unable to read Audio model mapping");
    } catch (...) { Close(); throw; }
#else
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream || stream.tellg()<=0)throw std::runtime_error("unable to open Audio model file");
    bytes_.resize(static_cast<std::size_t>(stream.tellg()));stream.seekg(0,std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes_.data()),bytes_.size());
    if(!stream)throw std::runtime_error("unable to read Audio model file");
    data_=bytes_.data();size_=bytes_.size();
#endif
  }
  ~ModelBytes(){Close();}
  ModelBytes(const ModelBytes&)=delete;
  ModelBytes& operator=(const ModelBytes&)=delete;
  const std::uint8_t* data()const{return data_;}
  std::size_t size()const{return size_;}
 private:
  void Close()noexcept{
#if defined(_WIN32)
    if(data_)UnmapViewOfFile(data_);
    if(mapping_)CloseHandle(mapping_);
    if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);
    mapping_=nullptr;file_=INVALID_HANDLE_VALUE;
#endif
    data_=nullptr;size_=0;
  }
  const std::uint8_t* data_=nullptr;std::size_t size_=0;
#if defined(_WIN32)
  HANDLE file_=INVALID_HANDLE_VALUE,mapping_=nullptr;
#else
  std::vector<std::uint8_t> bytes_;
#endif
};
}  // namespace

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
        throw std::runtime_error("TfLiteModelCreate failed for Audio model buffer");
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

 private:
  void CopyOutput(int output_index, void* destination, std::size_t bytes) {
    RequireOk(TfLiteTensorCopyToBuffer(
                  TfLiteInterpreterGetOutputTensor(interpreter_, output_index),
                  destination, bytes),
              "copy model output");
  }

  void ValidateContract() {
    if (TfLiteInterpreterGetInputTensorCount(interpreter_) != 3) {
      throw std::runtime_error("Audio model must expose exactly 3 inputs");
    }
    if (TfLiteInterpreterGetOutputTensorCount(interpreter_) !=
        static_cast<int>(kOutputCount)) {
      throw std::runtime_error("Audio model must expose exactly 7 outputs");
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

}  // namespace cleanwave

