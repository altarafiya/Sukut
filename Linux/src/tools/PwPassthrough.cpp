// Stage 2 tool: route audio through Sukut's PipeWire path without the model.
//   sukut_pw_passthrough [--seconds N] [--output NODE] [--prebuffer FRAMES] [--no-default]
//   sukut_pw_passthrough --restore      undo a routing change left by a crash
// Prints one line per event so tests can follow it: STARTED, STATS, STOPPED.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "audio/PipeWireBridge.h"

namespace {
std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop = true; }

void PrintStats(const sukut::linux_audio::BridgeStats& s) {
  std::cout << "STATS captured=" << s.captured_frames << " played=" << s.played_frames
            << " underruns=" << s.underruns << " overruns=" << s.overruns << " fill=" << s.fill_frames
            << " volume=" << s.volume << " muted=" << (s.muted ? 1 : 0) << std::endl;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace sukut::linux_audio;
  PipeWireBridge::Options options;
  int seconds = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--restore") {
      std::string message;
      const bool ok = PipeWireBridge::RestoreFromJournal(message);
      std::cout << (ok ? "RESTORED " : "RESTORE_FAILED ") << message << std::endl;
      return ok ? 0 : 1;
    } else if (arg == "--seconds" && has_value) {
      seconds = std::atoi(argv[++i]);
    } else if (arg == "--output" && has_value) {
      options.output = argv[++i];
    } else if (arg == "--prebuffer" && has_value) {
      options.prebuffer_frames = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (arg == "--no-default") {
      options.route = false;
    } else {
      std::cerr << "usage: sukut_pw_passthrough [--seconds N] [--output NODE] [--prebuffer FRAMES] [--no-default] | --restore\n";
      return 2;
    }
  }
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  PipeWireBridge bridge;
  std::string error;
  if (!bridge.Start(options, error)) {
    std::cout << "START_FAILED " << error << std::endl;
    return 1;
  }
  std::cout << "STARTED output=" << bridge.Output() << " sink=" << kSinkName << std::endl;
  const auto begin = std::chrono::steady_clock::now();
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
    if (elapsed % 1000 < 250) PrintStats(bridge.Stats());
    if (seconds > 0 && elapsed >= seconds * 1000) break;
  }
  const auto last = bridge.Stats();
  bridge.Stop();
  std::cout << "STOPPED" << std::endl;
  PrintStats(last);
  return 0;
}
