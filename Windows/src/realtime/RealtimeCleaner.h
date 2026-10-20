#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>

#include "realtime/PacketProcessor.h"
#include "dsp/SourceMixRange.h"

namespace cleanwave {

struct RealtimeMix {
  int voice_db = 0, music_db = -10, other_db = -20;
  int reduction_percent = 100, boost_db = 0;
};

struct RecoverySnapshot {
  std::uint64_t packets = 0, ai_runs = 0, bypasses = 0, probes = 0;
  std::uint64_t off_packets = 0;
  std::uint64_t fallbacks = 0, recoveries = 0, probe_failures = 0;
  std::uint64_t burst_covered_overruns = 0;
  double burst_allowance_ms = 0, burst_credit_ms = 0;
  bool paused = false;
  ProcessingMode applied_mode = ProcessingMode::Default;
  std::uint64_t applied_revision = 0;
};

struct CleanerControlSnapshot {
  ProcessingMode mode = ProcessingMode::Default;
  RealtimeMix effective, custom;
  std::uint64_t revision = 1;
};

// DSP/model/level are process-worker-owned. Configure/Snapshot are thread-safe.
class RealtimeCleaner final : public PacketProcessor {
 public:
  RealtimeCleaner(const std::filesystem::path& model,
                         const std::filesystem::path& rise,
                         const std::filesystem::path& hop,
                         bool buffered_recovery = false,
                         std::chrono::milliseconds burst_allowance = std::chrono::milliseconds(40),
                         bool collect_diagnostics = true);
  void Configure(RealtimeMix mix, bool silence_recovery = false);
  void SetMode(ProcessingMode mode, bool silence_recovery = false);
  CleanerControlSnapshot Controls() const;
  bool Warmup(std::stop_token stop) override;
  ProcessInfo Process(std::span<const std::int16_t> input, bool first,
                      AudioLevel::PcmHop& output) override;
  RecoverySnapshot Snapshot() const;
  // Diagnostic only: call on the owning worker or after the pipeline has joined.
  SeparationModel::StateBuffer ModelStateSnapshot() const;

 private:
  struct Configuration {
    RealtimeMix mix;
    RealtimeMix custom;
    ProcessingMode mode = ProcessingMode::Default;
    bool silence_recovery = false;
    int voice_q12 = 4096, music_q12 = 1295, other_q12 = 410;
    std::uint64_t revision = 1;
  };
  Configuration ReadConfiguration() const;
  static void SetEffectiveMix(Configuration& config);
  void ApplyConfiguration(const Configuration& config);
  StreamingDsp dsp_;
  AudioLevel level_;
  RecoveryController recovery_;
  std::array<float, StreamingDsp::kFirstInputFrames * 2> decoded_{};
  StreamingDsp::HopStereo dry_{}, wet_{};
  StreamingDsp::HopStereo silent_dry_{};
  mutable std::mutex config_mutex_, stats_mutex_;
  Configuration config_, applied_;
  RecoverySnapshot stats_;
  bool configuration_applied_ = false;
  bool stream_started_ = false;
  bool recovery_was_silent_ = false;
  bool collect_diagnostics_ = true;
  std::uint64_t fallback_offset_ = 0, recovery_offset_ = 0, probe_failure_offset_ = 0;
  std::uint64_t burst_covered_offset_ = 0;
};

}  // namespace cleanwave
