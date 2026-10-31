#pragma once

// The host's control of the live session. Everything that touches the session
// runs on one worker thread, so slow work (engine warmup, PipeWire setup) never
// blocks the tray or the socket. Mirrors the Windows host
// (Windows/src/sukut-host/main.cpp): the same restart policy (3 attempts at
// 1, 2 and 4 s, reset after a healthy minute) and closed-source-app rule.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "audio/SukutSession.h"
#include "host/HostSettings.h"

namespace sukut::host {

struct EngineState {
  int mode = 0;                // the mode running now; 0 = off
  int state = 0;               // 0 off, 1 running, 2 attention
  bool busy = false;           // a command is being applied
  bool standby = false;        // 5 s of silence
  bool reconnecting = false;   // a restart is scheduled
  std::string output;          // where Sukut plays now
  std::uint32_t source_pid = 0;  // the app the running session filters; 0 = all system audio
  int buffering = -1;          // the running session's buffering; -1 while off
  std::string attention;       // "source_closed", or the engine's error (state 2)
  std::uint64_t underruns = 0, fallbacks = 0;
  bool priority_set = false;
  std::uint64_t revision = 0;  // increases on every change the tray or window should show
};

class HostEngine {
 public:
  explicit HostEngine(std::filesystem::path engine_dir);
  ~HostEngine();
  HostEngine(const HostEngine&) = delete;
  HostEngine& operator=(const HostEngine&) = delete;

  // Applies a mode (0 = Off) with these settings and waits for the result.
  bool Select(int mode, const HostSettings& settings, std::string& error);
  // The same without waiting; `done` runs on the worker.
  void SelectAsync(int mode, HostSettings settings, std::function<void(bool, std::string)> done = {});
  void SetAmplification(int db);
  [[nodiscard]] EngineState State() const;

  // Called about once a second by the host. Queues a health check and returns
  // routing facts found since the last call, which the host saves:
  // "output=<node>" (Sukut followed an output) and "source_closed".
  std::vector<std::string> Supervise();
  void Shutdown();  // stop and give the audio back; waits

 private:
  void Post(std::function<void()> task);
  void Run();
  bool Apply(int mode, const HostSettings& settings, bool by_user, std::string& error);  // worker only
  void Tick();                                                                           // worker only
  void Update(const std::function<void(EngineState&)>& change);

  std::filesystem::path engine_dir_;

  // Worker-owned.
  std::unique_ptr<linux_audio::SukutSession> session_;
  HostSettings applied_;     // the settings the session runs with
  int applied_mode_ = 0;
  int restart_mode_ = 0;     // the mode a scheduled restart brings back
  int attempts_ = 0;
  std::optional<std::chrono::steady_clock::time_point> restart_at_, healthy_since_;

  mutable std::mutex state_mutex_;
  EngineState state_;
  std::vector<std::string> facts_;
  bool tick_queued_ = false;

  std::mutex queue_mutex_;
  std::condition_variable queue_ready_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false, shut_down_ = false;
  std::thread worker_;
};

}  // namespace sukut::host
