#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace cleanwave {

class SeparationModel final {
 public:
  static constexpr std::size_t kSpectralFloats = 1024;
  static constexpr std::size_t kStateFloats = 6 * 52 * 48;
  static constexpr std::size_t kOutputCount = 7;

  using SpectralBuffer = std::array<float, kSpectralFloats>;
  using StateBuffer = std::array<float, kStateFloats>;

  struct SourceHeads {
    SpectralBuffer voice_left{};
    SpectralBuffer voice_right{};
    SpectralBuffer music_left{};
    SpectralBuffer music_right{};
  };

  explicit SeparationModel(const std::filesystem::path& model_path);
  ~SeparationModel();

  SeparationModel(const SeparationModel&) = delete;
  SeparationModel& operator=(const SeparationModel&) = delete;
  SeparationModel(SeparationModel&&) noexcept;
  SeparationModel& operator=(SeparationModel&&) noexcept;

  void Reset();
  void Process(const SpectralBuffer& left, const SpectralBuffer& right,
               SourceHeads& outputs);
  [[nodiscard]] StateBuffer StateSnapshot() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cleanwave
