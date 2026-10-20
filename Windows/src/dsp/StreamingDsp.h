#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "model/SeparationModel.h"

namespace cleanwave {

class StreamingDsp final {
 public:
  static constexpr std::size_t kSampleRate = 48000;
  static constexpr std::size_t kFftSize = 1536;
  static constexpr std::size_t kHopFrames = 960;
  static constexpr std::size_t kOverlapFrames = 576;
  static constexpr std::size_t kDirectFrames = kHopFrames - kOverlapFrames;
  static constexpr std::size_t kFirstInputFrames = kFftSize;
  static constexpr std::size_t kSpectrumBins = kFftSize / 2 + 1;
  static constexpr std::size_t kModelBins = 512;
  static constexpr int kQ12One = 4096;

  struct ComplexBin {
    float real = 0.0F;
    float imag = 0.0F;
  };

  using Spectrum = std::array<ComplexBin, kSpectrumBins>;
  using HopStereo = std::array<float, kHopFrames * 2>;

  struct HopTrace {
    Spectrum original_left{};
    Spectrum original_right{};
    Spectrum remixed_left{};
    Spectrum remixed_right{};
    SeparationModel::SpectralBuffer model_left{};
    SeparationModel::SpectralBuffer model_right{};
    SeparationModel::SourceHeads heads{};
    SeparationModel::StateBuffer state_before{};
    SeparationModel::StateBuffer state_after{};
  };

  StreamingDsp(const std::filesystem::path& model_path,
                      const std::filesystem::path& rise_window_path,
                      const std::filesystem::path& hop_window_path);
  ~StreamingDsp();

  StreamingDsp(const StreamingDsp&) = delete;
  StreamingDsp& operator=(const StreamingDsp&) = delete;
  StreamingDsp(StreamingDsp&&) noexcept;
  StreamingDsp& operator=(StreamingDsp&&) noexcept;

  void Process(std::span<const float> interleaved_stereo, bool first,
               bool run_model, int voice_q12, int music_q12, int other_q12,
               HopStereo& dry_output, HopStereo& wet_output,
               HopTrace* trace = nullptr);

  void ResetNeuralState();
  void ResetAll();

  [[nodiscard]] SeparationModel::StateBuffer ModelStateSnapshot() const;
  [[nodiscard]] double WindowComplementRelativeError() const;

  static std::vector<float> DecodeQ15Window(const std::filesystem::path& path,
                                             std::size_t expected_samples);
  static SeparationModel::SpectralBuffer PackModelInput(
      const Spectrum& spectrum);
  static Spectrum RemixSpectrum(
      const Spectrum& original,
      const SeparationModel::SpectralBuffer& voice,
      const SeparationModel::SpectralBuffer& music, int voice_q12,
      int music_q12, int other_q12);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cleanwave
