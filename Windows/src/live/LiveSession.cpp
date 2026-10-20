#include <windows.h>
#include "live/LiveSession.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace cleanwave {
LiveSession::LiveSession(const std::filesystem::path& model,
    const std::filesystem::path& rise, const std::filesystem::path& hop, AudioRoute route, bool schedule_processing_audio,
    int buffering_level, bool collect_diagnostics, std::shared_ptr<std::atomic<int>> amplification_db, bool direct_playback)
    : route_(std::move(route)), buffering_(AudioBuffering::ForLevel(buffering_level)), amplification_db_(std::move(amplification_db)),
      cleaner_(std::make_shared<RealtimeCleaner>(model, rise, hop, true, std::chrono::milliseconds(buffering_.burst_ms),collect_diagnostics)),
      schedule_processing_audio_(schedule_processing_audio), collect_diagnostics_(collect_diagnostics), direct_playback_(direct_playback) {}
LiveSession::~LiveSession() { Stop(); }

void LiveSession::Start() {
  if (pipeline_ && pipeline_->Snapshot().active) throw std::logic_error("live session already running");
  if (pipeline_ && !pipeline_->WaitStopped(std::chrono::seconds(3)))
    throw std::runtime_error("previous workers have not stopped");
  try {
    ValidateAudioRoute(route_, EnumerateAudioEndpoints());
    capture_ = std::make_shared<WasapiCapture>(route_.capture_id, 48000, true, buffering_.capture_ms);
    render_ = std::make_shared<WasapiRender>(route_.physical_render_id, 48000, buffering_.render_ms, buffering_.prefill_ms, amplification_db_, !direct_playback_);
    PipelineOptions options; options.schedule_processing_audio = schedule_processing_audio_;
    options.collect_diagnostics = collect_diagnostics_;
    options.direct_playback = direct_playback_;
    pipeline_ = std::make_unique<BufferedAudioPipeline>(cleaner_, capture_, render_, options);
    error_.clear();
    ++generation_;
    last_render_frames_ = 0; last_output_progress_ = std::chrono::steady_clock::now();
    pipeline_->Start();
    state_ = LiveState::Running;
    Snapshot(); // preserve any synchronous startup worker failure
  } catch (const std::exception& e) {
    error_ = e.what(); state_ = LiveState::Faulted;
    if (pipeline_) { pipeline_->RequestStop(); pipeline_->WaitStopped(std::chrono::seconds(3)); }
    throw;
  }
}

bool LiveSession::Stop(std::chrono::milliseconds timeout) {
  if (pipeline_) {
    state_ = error_.empty() ? LiveState::Stopped : LiveState::Faulted;
    Snapshot();
    pipeline_->RequestStop();
    if (!pipeline_->WaitStopped(timeout)) {
      if (error_.empty()) error_ = "live shutdown timeout: workers still own their audio clients";
      state_ = LiveState::Faulted;
      return false;
    }
    Snapshot();
  }
  state_ = error_.empty() ? LiveState::Stopped : LiveState::Faulted;
  return true;
}

void LiveSession::Restart() {
  if (!Stop()) throw std::runtime_error(error_);
  Start();
}

void LiveSession::SelectRoute(AudioRoute route) {
  if (pipeline_ && pipeline_->Snapshot().active) throw std::logic_error("stop before selecting another output");
  ValidateAudioRoute(route, EnumerateAudioEndpoints());
  route_ = std::move(route);
}

LiveSnapshot LiveSession::Snapshot(bool include_timing) {
  LiveSnapshot snapshot;
  if (pipeline_) {
    snapshot.pipeline = pipeline_->Snapshot(include_timing);
    if (!snapshot.pipeline.failure.empty()) {
      if (error_.empty()) error_ = snapshot.pipeline.failure;
      state_ = LiveState::Faulted;
    }
  }
  snapshot.state = state_; snapshot.error = error_; snapshot.generation = generation_;
  snapshot.cleaner = cleaner_->Snapshot(); snapshot.controls = cleaner_->Controls();
  if (capture_) snapshot.capture = capture_->Snapshot();
  if (render_) snapshot.render = render_->Snapshot();
  if (state_ == LiveState::Running) {
    const auto now = std::chrono::steady_clock::now();
    if (!snapshot.pipeline.active && error_.empty()) error_ = "audio workers stopped unexpectedly";
    // Silence/player pause is valid. Only stalled output with captured work
    // pending is a failure; do not treat an idle virtual endpoint as overload.
    if (snapshot.render.frames != last_render_frames_ || snapshot.pipeline.captured == snapshot.pipeline.played)
      last_output_progress_ = now;
    else if (now - last_output_progress_ > std::chrono::seconds(2) && error_.empty())
      error_ = "audio output stopped progressing while captured audio is waiting";
    last_render_frames_ = snapshot.render.frames;
    if (!error_.empty()) { state_ = LiveState::Faulted; snapshot.state = state_; snapshot.error = error_; }
  }
  return snapshot;
}

const char* ModeName(ProcessingMode mode) {
  switch (mode) {
    case ProcessingMode::Off: return "Off";
    case ProcessingMode::Default: return "Default";
    case ProcessingMode::Custom: return "Custom";
  }
  return "Invalid";
}
const char* StateName(LiveState state) {
  switch (state) {
    case LiveState::Stopped: return "Stopped";
    case LiveState::Running: return "Running";
    case LiveState::Faulted: return "Faulted";
  }
  return "Invalid";
}
namespace {
void Timing(std::ostream& out, const char* name, const TimingSummary& t) {
  out << "\"" << name << "\":{\"count\":" << t.count << ",\"over_20ms\":" << t.over_budget
      << ",\"p50_ms\":" << t.p50_ms << ",\"p95_ms\":" << t.p95_ms << ",\"p99_ms\":" << t.p99_ms
      << ",\"mean_ms\":" << t.mean_ms << ",\"max_ms\":" << t.max_ms
      << ",\"percentile_samples\":" << t.percentile_samples << "}";
}
void Native(std::ostream& out, const char* name, const WasapiSnapshot& s) {
  out << "\"" << name << "\":{\"open\":" << s.open << ",\"frames\":" << s.frames
      << ",\"client_rate\":" << s.client_rate << ",\"mix_rate\":" << s.mix_rate
      << ",\"mix_channels\":" << s.mix_channels << ",\"buffer_frames\":" << s.buffer_frames
      << ",\"reported_stream_latency_ms\":" << s.reported_stream_latency_ms
      << ",\"startup_discontinuities\":" << s.startup_discontinuities
      << ",\"discontinuities\":" << s.discontinuities << ",\"timestamp_errors\":" << s.timestamp_errors
      << ",\"wait_timeouts\":" << s.wait_timeouts
      << ",\"mmcss_registered\":" << s.mmcss_registered << ",\"mmcss_error\":" << s.mmcss_error
      << ",\"empty_render_observations\":" << s.empty_render_observations
      << ",\"capture_position_gaps\":" << s.capture_position_gaps << ",\"capture_missing_frames\":" << s.capture_missing_frames
      << ",\"render_clock_deficit_frames\":" << s.render_clock_deficit_frames << ",\"clock_read_errors\":" << s.clock_read_errors
      << ",\"max_capture_packet_interval_ms\":" << s.max_capture_packet_interval_ms
      << ",\"max_capture_read_interval_ms\":" << s.max_capture_read_interval_ms
      << ",\"max_render_write_interval_ms\":" << s.max_render_write_interval_ms
      << ",\"startup_prefill_frames\":" << s.startup_prefill_frames
      << ",\"media_category_requested\":" << s.media_category_requested
      << ",\"high_qos\":" << s.high_qos << ",\"processing_thread_owns_schedule\":" << s.processing_thread_owns_schedule
      << ",\"high_resolution_timer\":" << s.high_resolution_timer << "}";
}
}
void WriteLiveStatus(const std::filesystem::path& path, const LiveSnapshot& s) {
  auto temp = path; temp += L".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write live status");
    SYSTEM_POWER_STATUS power{}; const bool has_power = GetSystemPowerStatus(&power) != FALSE;
    out << std::boolalpha << std::setprecision(10) << "{\n\"state\":\"" << StateName(s.state)
        << "\",\"error\":\"" << EscapeJson(s.error) << "\",\"ac_online\":" << (has_power ? static_cast<unsigned>(power.ACLineStatus) : 255)
        << ",\"generation\":" << s.generation
        << ",\"requested_mode\":\"" << ModeName(s.controls.mode)
        << "\",\"applied_mode\":\"" << ModeName(s.cleaner.applied_mode)
        << "\",\"requested_revision\":" << s.controls.revision
        << ",\"applied_revision\":" << s.cleaner.applied_revision
        << ",\"custom\":{\"voice_db\":" << s.controls.custom.voice_db
        << ",\"music_db\":" << s.controls.custom.music_db << ",\"other_db\":" << s.controls.custom.other_db
        << ",\"reduction_percent\":" << s.controls.custom.reduction_percent
        << ",\"boost_db\":" << s.controls.custom.boost_db << "},\n"
        << "\"pipeline\":{\"active\":" << s.pipeline.active << ",\"captured\":" << s.pipeline.captured
        << ",\"processed\":" << s.pipeline.processed << ",\"played\":" << s.pipeline.played
        << ",\"input_queue\":" << s.pipeline.captured_queue << ",\"output_queue\":" << s.pipeline.rendered_queue
        << ",\"input_idle_waits\":" << s.pipeline.input_idle_waits << ",\"output_idle_waits\":" << s.pipeline.output_idle_waits
        << ",\"input_high_water\":" << s.pipeline.captured_high_water
        << ",\"output_high_water\":" << s.pipeline.rendered_high_water << ",";
    out << "\"capture_segments_reset\":" << s.pipeline.capture_segments_reset
        << ",\"discarded_partial_frames\":" << s.pipeline.discarded_partial_frames
        << ",\"discarded_future_context_frames\":" << s.pipeline.discarded_future_context_frames << ',';
    Timing(out, "ai_engine", s.pipeline.ai_engine); out << ',';
    Timing(out, "ai_packet", s.pipeline.ai_packet); out << ',';
    Timing(out, "bypass_packet", s.pipeline.bypass_packet); out << ',';
    Timing(out, "ai_engine_cpu", s.pipeline.ai_engine_cpu);
    out << ",\"ai_engine_elapsed_minus_cpu_mean_ms\":" << s.pipeline.ai_engine_elapsed_minus_cpu_mean_ms
        << ",\"cpu_accounting_note\":\"GetThreadTimes is coarse; per-hop CPU percentiles do not establish scheduling stalls.\"";
    const auto& schedule = s.pipeline.processing_schedule;
    out << ",\"processing_schedule\":{\"requested\":" << schedule.requested
        << ",\"mmcss_registered\":" << schedule.mmcss_registered << ",\"priority_set\":" << schedule.priority_set
        << ",\"high_qos\":" << schedule.high_qos << ",\"mmcss_error\":" << schedule.mmcss_error
        << ",\"priority_error\":" << schedule.priority_error << ",\"qos_error\":" << schedule.qos_error
        << ",\"relative_priority\":" << schedule.relative_priority << "}},\n"
        << "\"cleaner\":{\"packets\":" << s.cleaner.packets << ",\"ai_runs\":" << s.cleaner.ai_runs
        << ",\"off_packets\":" << s.cleaner.off_packets << ",\"bypasses\":" << s.cleaner.bypasses
        << ",\"paused\":" << s.cleaner.paused << ",\"fallbacks\":" << s.cleaner.fallbacks
        << ",\"recoveries\":" << s.cleaner.recoveries << ",\"probes\":" << s.cleaner.probes
        << ",\"probe_failures\":" << s.cleaner.probe_failures
        << ",\"burst_allowance_ms\":" << s.cleaner.burst_allowance_ms
        << ",\"burst_credit_ms\":" << s.cleaner.burst_credit_ms
        << ",\"burst_covered_overruns\":" << s.cleaner.burst_covered_overruns << "},\n";
    Native(out, "capture", s.capture); out << ','; Native(out, "render", s.render);
    out << ",\n\"restore\":\"If a source app was routed to CABLE Input, restore its prior output before stopping CleanWave.\"\n}\n";
    out.flush(); if (!out) throw std::runtime_error("live status write failed");
  }
  // MoveFileEx avoids a missing-file interval for polling controllers on Windows.
  for (unsigned attempt = 0; attempt < 10; ++attempt) {
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
    const DWORD error = GetLastError();
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
    Sleep(10); // Controller only; no audio worker waits on telemetry readers.
  }
  throw std::runtime_error("live status atomic replacement failed");
}
} // namespace cleanwave
