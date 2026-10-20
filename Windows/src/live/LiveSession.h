#pragma once

#include "audio/AudioEndpoints.h"
#include "live/AudioBuffering.h"
#include "audio/WasapiAdapters.h"
#include "realtime/RealtimeCleaner.h"

namespace cleanwave {
enum class LiveState { Stopped, Running, Faulted };
struct LiveSnapshot {
  LiveState state = LiveState::Stopped;
  std::string error;
  std::uint64_t generation = 0;
  PipelineSnapshot pipeline;
  RecoverySnapshot cleaner;
  CleanerControlSnapshot controls;
  WasapiSnapshot capture, render;
};

// Lifecycle and Snapshot belong to the controller thread. Mix/mode publication
// can run concurrently. Device identity is revalidated before every Start.
class LiveSession final {
 public:
  LiveSession(const std::filesystem::path& model, const std::filesystem::path& rise,
              const std::filesystem::path& hop, AudioRoute route, bool schedule_processing_audio = true,
              int buffering_level = 0, bool collect_diagnostics = true,
              std::shared_ptr<std::atomic<int>> amplification_db = {}, bool direct_playback = false);
  ~LiveSession();
  void Start();
  bool Stop(std::chrono::milliseconds timeout = std::chrono::seconds(3));
  void Restart();
  void SelectRoute(AudioRoute route); // only while stopped; never follows defaults
  void SetMode(ProcessingMode mode) { cleaner_->SetMode(mode); }
  void Configure(RealtimeMix mix, bool silence_recovery = false) { cleaner_->Configure(mix, silence_recovery); }
  LiveSnapshot Snapshot(bool include_timing = true);
 private:
  AudioRoute route_;
  AudioBuffering buffering_;
  std::shared_ptr<std::atomic<int>> amplification_db_;
  std::shared_ptr<RealtimeCleaner> cleaner_;
  std::shared_ptr<WasapiCapture> capture_;
  std::shared_ptr<WasapiRender> render_;
  std::unique_ptr<BufferedAudioPipeline> pipeline_;
  LiveState state_ = LiveState::Stopped;
  std::string error_;
  std::uint64_t generation_ = 0;
  bool schedule_processing_audio_;
  bool collect_diagnostics_;
  bool direct_playback_;
  std::uint64_t last_render_frames_ = 0;
  std::chrono::steady_clock::time_point last_output_progress_{};
};

const char* ModeName(ProcessingMode mode);
const char* StateName(LiveState state);
// Atomic replacement. No private captured audio is written by the controller.
void WriteLiveStatus(const std::filesystem::path& path, const LiveSnapshot& snapshot);
} // namespace cleanwave
