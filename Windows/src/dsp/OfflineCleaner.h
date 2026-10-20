#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

#include "dsp/AudioLevel.h"
#include "dsp/SourceMixRange.h"
#include "dsp/StreamingDsp.h"

namespace cleanwave {

class OfflineCleaner final {
 public:
  using PcmHop = AudioLevel::PcmHop;

  OfflineCleaner(const std::filesystem::path& model_path,
                        const std::filesystem::path& rise_window_path,
                        const std::filesystem::path& hop_window_path);

  void ProcessPcm16(std::span<const std::int16_t> interleaved_stereo,
                    bool first, PcmHop& output, bool paused = false,
                    bool run_model = true);

  void SetReductionPercent(int percent);
  void SetVoiceBoostDb(int db);
  void SetSourceMixDb(int voice_db, int music_db, int other_db);
  void ResetNeuralState();
  void ResetAll();

  [[nodiscard]] SeparationModel::StateBuffer ModelStateSnapshot() const;

  static int DbToQ12(int db);

 private:
  StreamingDsp dsp_;
  AudioLevel level_;
  std::array<float, StreamingDsp::kFirstInputFrames * 2> first_decoded_{};
  std::array<float, StreamingDsp::kHopFrames * 2> hop_decoded_{};
  StreamingDsp::HopStereo dry_{};
  StreamingDsp::HopStereo wet_{};
  int voice_q12_ = 4096;
  int music_q12_ = 1295;
  int other_q12_ = 410;
};

}  // namespace cleanwave
