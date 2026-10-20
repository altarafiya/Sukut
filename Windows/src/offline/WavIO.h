#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleanwave {

struct WavPcm16Stereo {
  std::uint32_t sample_rate = 48000;
  std::vector<std::int16_t> samples;

  [[nodiscard]] std::size_t FrameCount() const { return samples.size() / 2; }
};

WavPcm16Stereo ReadPcm16StereoWav(const std::filesystem::path& path);
void WritePcm16StereoWav(const std::filesystem::path& path,
                         const WavPcm16Stereo& wav);

}  // namespace cleanwave
