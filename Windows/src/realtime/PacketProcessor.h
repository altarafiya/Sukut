#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <stop_token>

#include "dsp/AudioLevel.h"
#include "realtime/RecoveryController.h"

namespace cleanwave {

enum class ProcessingMode { Off, Default, Custom };

struct ProcessInfo {
  CleanerWork work = CleanerWork::Run;
  bool paused = false;
  bool reset_neural_state = false;
  std::chrono::nanoseconds engine_time{};  // decode + FFT/model/remix/synthesis
  std::chrono::nanoseconds total_time{};   // engine + level/render/reset/config
  std::uint64_t mix_revision = 0;
  int voice_q12 = 4096, music_q12 = 1295, other_q12 = 410;
  ProcessingMode mode = ProcessingMode::Default;
  std::chrono::nanoseconds engine_cpu_time{}; // diagnostic only; recovery uses elapsed engine_time
  bool engine_cpu_time_valid = false;
};

class PacketProcessor {
 public:
  virtual ~PacketProcessor() = default;
  // Called only on the process worker before capture starts. Reset each stream.
  virtual bool Warmup(std::stop_token stop) = 0;
  virtual ProcessInfo Process(std::span<const std::int16_t> input, bool first,
                              AudioLevel::PcmHop& output) = 0;
};

}  // namespace cleanwave
