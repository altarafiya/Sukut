#include "realtime/RealtimeCleaner.h"

#include <algorithm>
#include <stdexcept>

#include "dsp/OfflineCleaner.h"
#include "realtime/ProcessingThreadTask.h"

namespace cleanwave {
using Clock = std::chrono::steady_clock;

RealtimeCleaner::RealtimeCleaner(
    const std::filesystem::path& model, const std::filesystem::path& rise,
    const std::filesystem::path& hop, bool buffered_recovery, std::chrono::milliseconds burst_allowance, bool collect_diagnostics)
    : dsp_(model, rise, hop), recovery_(buffered_recovery ? burst_allowance
                                                       : std::chrono::milliseconds{}), collect_diagnostics_(collect_diagnostics) {}

void RealtimeCleaner::Configure(RealtimeMix mix, bool silence_recovery) {
  mix.voice_db = std::clamp(mix.voice_db, kSourceMixMinDb, kSourceMixMaxDb);
  mix.music_db = std::clamp(mix.music_db, kSourceMixMinDb, kSourceMixMaxDb);
  mix.other_db = std::clamp(mix.other_db, kSourceMixMinDb, kSourceMixMaxDb);
  mix.reduction_percent = std::clamp(mix.reduction_percent, 0, 100);
  mix.boost_db = std::clamp(mix.boost_db, 0, 6);
  std::lock_guard lock(config_mutex_);
  config_.custom = mix;
  config_.silence_recovery = silence_recovery;
  config_.mode = ProcessingMode::Custom;
  SetEffectiveMix(config_);
  ++config_.revision;
}

void RealtimeCleaner::SetEffectiveMix(Configuration& config) {
  config.mix = config.mode == ProcessingMode::Custom ? config.custom : RealtimeMix{};
  if (config.mode == ProcessingMode::Off) {
    config.mix.reduction_percent = 0;
    config.mix.boost_db = 0;
  }
  config.voice_q12 = OfflineCleaner::DbToQ12(config.mix.voice_db);
  config.music_q12 = OfflineCleaner::DbToQ12(config.mix.music_db);
  config.other_q12 = OfflineCleaner::DbToQ12(config.mix.other_db);
}

void RealtimeCleaner::SetMode(ProcessingMode mode, bool silence_recovery) {
  if (mode != ProcessingMode::Off && mode != ProcessingMode::Default &&
      mode != ProcessingMode::Custom) throw std::invalid_argument("invalid cleaner mode");
  std::lock_guard lock(config_mutex_);
  config_.mode = mode;
  config_.silence_recovery = silence_recovery;
  SetEffectiveMix(config_);
  ++config_.revision;
}

CleanerControlSnapshot RealtimeCleaner::Controls() const {
  const auto config = ReadConfiguration();
  return {config.mode, config.mix, config.custom, config.revision};
}

RealtimeCleaner::Configuration
RealtimeCleaner::ReadConfiguration() const {
  std::lock_guard lock(config_mutex_);
  return config_;
}

void RealtimeCleaner::ApplyConfiguration(const Configuration& config) {
  if (!configuration_applied_ ||
      applied_.mix.reduction_percent != config.mix.reduction_percent) {
    level_.SetReductionPercent(config.mix.reduction_percent);
  }
  if (!configuration_applied_ || applied_.mix.boost_db != config.mix.boost_db) {
    level_.SetVoiceBoostDb(config.mix.boost_db);
  }
  applied_ = config;
  configuration_applied_ = true;
}

bool RealtimeCleaner::Warmup(std::stop_token stop) {
  ApplyConfiguration(ReadConfiguration());
  decoded_.fill(0.0F);
  dsp_.ResetAll();
  for (unsigned i = 0; i < 4; ++i) {
    if (stop.stop_requested()) return false;
    const auto samples = (i == 0 ? StreamingDsp::kFirstInputFrames
                                 : StreamingDsp::kHopFrames) * 2;
    dsp_.Process(std::span(decoded_).first(samples), i == 0, true,
                 applied_.voice_q12, applied_.music_q12, applied_.other_q12,
                 dry_, wet_);
  }
  dsp_.ResetAll();
  level_.ResetStreamState();
  recovery_.Reset();
  fallback_offset_ = recovery_offset_ = probe_failure_offset_ = 0;
  burst_covered_offset_ = 0;
  stream_started_ = false;
  recovery_was_silent_ = false;
  {
    std::lock_guard lock(stats_mutex_);
    stats_ = {};
    stats_.burst_allowance_ms = std::chrono::duration<double,std::milli>(recovery_.BurstAllowance()).count();
    stats_.burst_credit_ms = std::chrono::duration<double,std::milli>(recovery_.BurstCredit()).count();
  }
  return !stop.stop_requested();
}

ProcessInfo RealtimeCleaner::Process(
    std::span<const std::int16_t> input, bool first, AudioLevel::PcmHop& output) {
  const auto total_start = Clock::now();
  const std::size_t expected = (first ? StreamingDsp::kFirstInputFrames
                                     : StreamingDsp::kHopFrames) * 2;
  if (input.size() != expected) throw std::runtime_error("invalid realtime PCM packet");
  const auto config = ReadConfiguration();
  const bool off = config.mode == ProcessingMode::Off;
  const bool mode_transition = configuration_applied_ &&
      (off != (applied_.mode == ProcessingMode::Off));
  const bool segment_reset = first && stream_started_;
  ApplyConfiguration(config);
  if (mode_transition || segment_reset) {
    if (segment_reset) dsp_.ResetAll();
    else dsp_.ResetNeuralState();
    fallback_offset_ += recovery_.FallbackEvents();
    recovery_offset_ += recovery_.Recoveries();
    probe_failure_offset_ += recovery_.ProbeFailures();
    burst_covered_offset_ += recovery_.CoveredOverruns();
    recovery_.Reset();
    if (off || segment_reset) level_.ResetStreamState();
    recovery_was_silent_ = false;
  }
  stream_started_ = true;
  const auto work = off ? CleanerWork::Bypass : recovery_.NextWork();
  const auto engine_start = Clock::now();
  const auto cpu_start = collect_diagnostics_ ? ReadThreadCpuTime() : ThreadCpuSample{};
  auto decoded = std::span(decoded_).first(expected);
  DecodeStereoPcm16(input, decoded);
  dsp_.Process(decoded, first, work != CleanerWork::Bypass,
               config.voice_q12, config.music_q12, config.other_q12, dry_, wet_);
  const auto engine_time = Clock::now() - engine_start;
  const auto cpu_end = collect_diagnostics_ ? ReadThreadCpuTime() : ThreadCpuSample{};
  const bool cpu_valid = cpu_start.valid && cpu_end.valid && cpu_end.nanoseconds >= cpu_start.nanoseconds;
  const auto cpu_time = cpu_valid ? std::chrono::nanoseconds(cpu_end.nanoseconds - cpu_start.nanoseconds) : std::chrono::nanoseconds{};
  const bool reset = !off && recovery_.Complete(work, engine_time);
  if (off) {
    // Aligned first 960 frames. PCM16 -> float -> PCM16 is exact.
    for (std::size_t i = 0; i < output.size(); ++i)
      output[i] = static_cast<std::int16_t>(dry_[i] * 32768.0F);
  } else {
    if (first) level_.ResetAmount();
    // Sukut's Halal preset must not expose dry music during recovery or the
    // first recovery fade. Existing frontends keep their original behavior.
    const bool quiet_recovery = config.silence_recovery && (recovery_.Paused() || recovery_was_silent_);
    level_.Render(quiet_recovery ? silent_dry_ : dry_, wet_, output, recovery_.Paused());
    if (config.silence_recovery && recovery_.Paused()) output.fill(0);
    recovery_was_silent_ = config.silence_recovery && recovery_.Paused();
  }
  if (reset) dsp_.ResetNeuralState();
  const auto total_time = Clock::now() - total_start;
  {
    std::lock_guard lock(stats_mutex_);
    ++stats_.packets;
    if (work == CleanerWork::Bypass) ++stats_.bypasses;
    else ++stats_.ai_runs;
    if (work == CleanerWork::RecoveryProbe) ++stats_.probes;
    if (off) ++stats_.off_packets;
    stats_.paused = recovery_.Paused();
    stats_.applied_mode = config.mode;
    stats_.applied_revision = config.revision;
    stats_.fallbacks = fallback_offset_ + recovery_.FallbackEvents();
    stats_.recoveries = recovery_offset_ + recovery_.Recoveries();
    stats_.probe_failures = probe_failure_offset_ + recovery_.ProbeFailures();
    stats_.burst_covered_overruns = burst_covered_offset_ + recovery_.CoveredOverruns();
    stats_.burst_allowance_ms = std::chrono::duration<double,std::milli>(recovery_.BurstAllowance()).count();
    stats_.burst_credit_ms = std::chrono::duration<double,std::milli>(recovery_.BurstCredit()).count();
  }
  return {work, recovery_.Paused(), reset || mode_transition || segment_reset, engine_time, total_time,
          config.revision, config.voice_q12, config.music_q12, config.other_q12, config.mode, cpu_time, cpu_valid};
}

RecoverySnapshot RealtimeCleaner::Snapshot() const {
  std::lock_guard lock(stats_mutex_);
  return stats_;
}

SeparationModel::StateBuffer RealtimeCleaner::ModelStateSnapshot() const {
  return dsp_.ModelStateSnapshot();
}

}  // namespace cleanwave
