#pragma once

// Sukut's live filtering session on Linux: the shared engine (RealtimeCleaner in
// the Windows BufferedAudioPipeline) between PipeWire capture and playback.
// Mirrors the Windows host: same mode presets, buffering table, Standby and
// direct playback (Windows/src/sukut/Bridge.cpp, live/LiveSession.cpp).
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "audio/PipeWireAdapters.h"
#include "audio/PipeWireBridge.h"
#include "realtime/RealtimeCleaner.h"

namespace sukut::linux_audio {

struct SessionSettings {
  std::filesystem::path engine_dir;  // sealed separator.tflite, window_576_q15.bin, window_960_q15.bin
  int mode = 1;                      // 1 Music Filter, 2 Optimized, 3 Strict
  int buffering = 1;                 // 0 Lowest, 1 Balanced, 2 Maximum
  int optimized_background_db = 0;   // -20..+10, Optimized only
  int amplification_db = 0;          // 0..12
  std::string output;                // real output node.name; empty = current default
  bool fallback_output = false;      // a missing output: play on the default until it appears (the host)
  std::uint32_t source_pid = 0;      // one app (process id); 0 = all system audio
  bool route = true;                 // route audio through Sukut once the engine is ready
  bool diagnostics = false;          // per-hop timing percentiles
  bool tap = false;                  // keep engine input/output for the parity test
};

struct SessionSnapshot {
  BridgeStats bridge;
  cleanwave::RecoverySnapshot cleaner;
  cleanwave::PipelineSnapshot pipeline;
};

class SukutSession {
 public:
  SukutSession();
  ~SukutSession();
  SukutSession(const SukutSession&) = delete;
  SukutSession& operator=(const SukutSession&) = delete;

  bool Start(const SessionSettings& settings, std::string& error);
  void SetMode(int mode, int optimized_background_db);  // live, no restart
  void SetAmplification(int db) { amplification_db_->store(std::clamp(db, 0, 12)); }  // live
  void Stop();
  [[nodiscard]] SessionSnapshot Snapshot(bool timing = false) const;
  [[nodiscard]] std::string Output() const { return bridge_.Output(); }
  // Routing events (output followed, app streams targeted, app closed); see PipeWireBridge.
  std::vector<std::string> TakeEvents() { bridge_.CheckSource(); return bridge_.TakeEvents(); }
  [[nodiscard]] const EngineTap& Tap() const { return tap_; }

  // Windows host presets: voice 0 dB, music -100 dB in every filtering mode;
  // background 0 (Music Filter), -20..+10 (Optimized) or -100 (Strict).
  static cleanwave::RealtimeMix MixFor(int mode, int optimized_background_db);

 private:
  PipeWireBridge bridge_;
  std::shared_ptr<cleanwave::RealtimeCleaner> cleaner_;
  std::shared_ptr<PipeWireInput> input_;
  std::shared_ptr<PipeWireOutput> output_;
  std::unique_ptr<cleanwave::BufferedAudioPipeline> pipeline_;
  std::shared_ptr<std::atomic<int>> amplification_db_ = std::make_shared<std::atomic<int>>(0);
  EngineTap tap_;
};

}  // namespace sukut::linux_audio
