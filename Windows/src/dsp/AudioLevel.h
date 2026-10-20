#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "dsp/StreamingDsp.h"

namespace cleanwave {

class AudioLevel final {
 public:
  using PcmHop = std::array<std::int16_t, StreamingDsp::kHopFrames * 2>;

  void SetReductionPercent(int percent);
  void SetVoiceBoostDb(int db);
  void ResetAmount();
  void ResetStreamState();

  void Render(const StreamingDsp::HopStereo& dry,
              const StreamingDsp::HopStereo& wet, PcmHop& output,
              bool paused);

  [[nodiscard]] float Reduction() const { return reduction_; }
  [[nodiscard]] int VoiceBoostDb() const { return boost_db_; }

 private:
  float reduction_ = 1.0F;
  int boost_db_ = 0;
  float boost_gain_ = 1.0F;
  float amount_ = 1.0F;
  float gain_ = 1.0F;
};

void DecodeStereoPcm16(std::span<const std::int16_t> pcm,
                       std::span<float> destination);

}  // namespace cleanwave
