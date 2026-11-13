#include "audio/SukutSession.h"

#include <algorithm>
#include <chrono>

#include "live/AudioBuffering.h"

namespace sukut::linux_audio {

SukutSession::SukutSession() = default;
SukutSession::~SukutSession() { Stop(); }

cleanwave::RealtimeMix SukutSession::MixFor(int mode, int optimized_background_db, cleanwave::OptimizedMix mix) {
  const auto gains = cleanwave::ModeGains(mode, optimized_background_db, mix);
  return {gains.voice_db, gains.music_db, gains.background_db, 100, 0};
}

bool SukutSession::Start(const SessionSettings& settings, std::string& error) {
  if (pipeline_) { error = "already running"; return false; }
  if (settings.mode < 1 || settings.mode > 3) { error = "mode must be 1, 2 or 3"; return false; }
  try {
    const auto buffering = cleanwave::AudioBuffering::ForLevel(settings.buffering);
    amplification_db_->store(std::clamp(settings.amplification_db, 0, 12));
    const auto& dir = settings.engine_dir;
    cleaner_ = std::make_shared<cleanwave::RealtimeCleaner>(
        dir / "separator.tflite", dir / "window_576_q15.bin", dir / "window_960_q15.bin",
        true, std::chrono::milliseconds(buffering.burst_ms), settings.diagnostics);
    cleaner_->EnableEfficiency(true);  // Standby after 5 s of digital silence, >16 kHz guard
    cleaner_->Configure(MixFor(settings.mode, settings.optimized_background_db, settings.optimized_mix), true);

    // Buffering levels map onto the rings exactly as onto the Windows endpoints:
    // capture buffer, render buffer and render prefill.
    PipeWireBridge::Options options;
    options.output = settings.output;
    options.fallback_output = settings.fallback_output;
    options.source_pid = settings.source_pid;
    options.route = false;  // only once the engine is ready
    options.engine = true;
    options.capture_frames = buffering.capture_ms * kRate / 1000;
    options.playback_frames = buffering.render_ms * kRate / 1000;
    options.prebuffer_frames = buffering.prefill_ms * kRate / 1000;
    if (!bridge_.Start(options, error)) return false;

    tap_ = {};
    tap_.enabled = settings.tap;
    input_ = std::make_shared<PipeWireInput>(bridge_, &tap_);
    output_ = std::make_shared<PipeWireOutput>(bridge_, amplification_db_, &tap_);
    cleanwave::PipelineOptions pipeline_options;
    pipeline_options.schedule_processing_audio = true;
    pipeline_options.collect_diagnostics = settings.diagnostics;
    pipeline_options.direct_playback = true;  // as the Windows Sukut bridge
    pipeline_ = std::make_unique<cleanwave::BufferedAudioPipeline>(cleaner_, input_, output_, pipeline_options);
    pipeline_->Start();  // warmup and readiness barrier

    if (settings.route && !bridge_.Route(error)) { Stop(); return false; }
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    Stop();
    return false;
  }
}

void SukutSession::SetMode(int mode, int optimized_background_db, cleanwave::OptimizedMix mix) {
  if (cleaner_ && mode >= 1 && mode <= 3) cleaner_->Configure(MixFor(mode, optimized_background_db, mix), true);
}

void SukutSession::Stop() {
  // Give the apps back to the real output first, then stop the engine, then
  // remove Sukut's nodes (Linux/PLAN.md section 5.5).
  bridge_.RestoreRouting();
  if (pipeline_) {
    pipeline_->RequestStop();
    pipeline_->WaitStopped(std::chrono::seconds(3));
  }
  bridge_.Stop();
  pipeline_.reset();
  input_.reset();
  output_.reset();
}

SessionSnapshot SukutSession::Snapshot(bool timing) const {
  SessionSnapshot snapshot;
  snapshot.bridge = bridge_.Stats();
  if (cleaner_) snapshot.cleaner = cleaner_->Snapshot();
  if (pipeline_) snapshot.pipeline = pipeline_->Snapshot(timing);
  return snapshot;
}

}  // namespace sukut::linux_audio
