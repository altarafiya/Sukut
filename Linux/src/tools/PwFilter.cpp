// Stage 3 tool: live filtering through Sukut's engine on PipeWire.
//   sukut_pw_filter --engine DIR [--mode 1|2|3] [--buffering 0|1|2] [--background DB]
//                   [--amplify DB] [--output NODE] [--fallback] [--app PID] [--seconds N]
//                   [--no-route] [--diagnostics] [--tap DIR]
//   sukut_pw_filter --restore      undo routing left by a crash
// One line per event: STARTED, STATS (every second), routing events
// (OUTPUT_CHANGED, OUTPUT_LOST, SOURCE_TARGETED, SOURCE_CLOSED, FEEDBACK_LOOP,
// STREAM_REDIRECTED, STREAM_PINNED, EASYEFFECTS_PROCESS_ALL), STOPPED, TIMING.
// --fallback: a missing --output plays on the default until it appears, as the host does.
// With --app, the session ends when that app closes (exit code 3), as on Windows.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "audio/SukutSession.h"

namespace {
std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop = true; }

void Print(const sukut::linux_audio::SessionSnapshot& s) {
  std::cout << "STATS captured=" << s.bridge.captured_frames << " played=" << s.bridge.played_frames
            << " underruns=" << s.bridge.underruns << " overruns=" << s.bridge.overruns
            << " playing=" << (s.bridge.playing ? 1 : 0) << " fill=" << s.bridge.fill_frames
            << " packets=" << s.cleaner.packets << " ai_runs=" << s.cleaner.ai_runs
            << " standby=" << (s.cleaner.standby ? 1 : 0) << " standby_entries=" << s.cleaner.standby_entries
            << " fallbacks=" << s.cleaner.fallbacks << " recoveries=" << s.cleaner.recoveries
            << " segments_reset=" << s.pipeline.capture_segments_reset
            << " volume=" << s.bridge.volume << " muted=" << (s.bridge.muted ? 1 : 0) << std::endl;
}

void Save(const std::string& path, const std::vector<std::int16_t>& samples) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(std::int16_t)));
}
}  // namespace

int main(int argc, char** argv) {
  using namespace sukut::linux_audio;
  SessionSettings settings;
  int seconds = 0;
  std::string tap_dir;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool value = i + 1 < argc;
    if (arg == "--restore") {
      std::string message;
      const bool ok = PipeWireBridge::RestoreFromJournal(message);
      std::cout << (ok ? "RESTORED " : "RESTORE_FAILED ") << message << std::endl;
      return ok ? 0 : 1;
    } else if (arg == "--engine" && value) settings.engine_dir = argv[++i];
    else if (arg == "--mode" && value) settings.mode = std::atoi(argv[++i]);
    else if (arg == "--buffering" && value) settings.buffering = std::atoi(argv[++i]);
    else if (arg == "--background" && value) settings.optimized_background_db = std::atoi(argv[++i]);
    else if (arg == "--amplify" && value) settings.amplification_db = std::atoi(argv[++i]);
    else if (arg == "--output" && value) settings.output = argv[++i];
    else if (arg == "--fallback") settings.fallback_output = true;
    else if (arg == "--app" && value) settings.source_pid = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    else if (arg == "--seconds" && value) seconds = std::atoi(argv[++i]);
    else if (arg == "--no-route") settings.route = false;
    else if (arg == "--diagnostics") settings.diagnostics = true;
    else if (arg == "--tap" && value) { tap_dir = argv[++i]; settings.tap = true; }
    else {
      std::cerr << "usage: sukut_pw_filter --engine DIR [--mode N] [--buffering N] [--background DB] [--amplify DB]"
                   " [--output NODE] [--fallback] [--app PID] [--seconds N] [--no-route] [--diagnostics] [--tap DIR] | --restore\n";
      return 2;
    }
  }
  if (settings.engine_dir.empty()) { std::cerr << "--engine DIR is required\n"; return 2; }
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  SukutSession session;
  std::string error;
  if (!session.Start(settings, error)) {
    std::cout << "START_FAILED " << error << std::endl;
    return 1;
  }
  const auto first = session.Snapshot();
  std::cout << "STARTED output=" << session.Output() << " mode=" << settings.mode << " buffering=" << settings.buffering
            << " kernels=" << first.cleaner.kernels << std::endl;
  const auto begin = std::chrono::steady_clock::now();
  auto next = begin + std::chrono::seconds(1);
  bool source_closed = false;
  while (!g_stop && !source_closed) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto now = std::chrono::steady_clock::now();
    if (now >= next) {
      for (const auto& event : session.TakeEvents()) {
        std::cout << event << std::endl;
        source_closed = source_closed || event == "SOURCE_CLOSED";
      }
      Print(session.Snapshot());
      next += std::chrono::seconds(1);
    }
    if (seconds > 0 && now - begin >= std::chrono::seconds(seconds)) break;
  }
  const auto last = session.Snapshot(true);
  session.Stop();
  for (const auto& event : session.TakeEvents()) std::cout << event << std::endl;
  std::cout << "STOPPED" << (source_closed ? " reason=source_closed" : "") << std::endl;
  Print(last);
  const auto& t = last.pipeline.ai_engine;
  std::cout << "TIMING count=" << t.count << " mean_ms=" << t.mean_ms << " p50_ms=" << t.p50_ms << " p95_ms=" << t.p95_ms
            << " p99_ms=" << t.p99_ms << " max_ms=" << t.max_ms << " over_20ms=" << t.over_budget
            << " priority_set=" << (last.pipeline.processing_schedule.priority_set ? 1 : 0) << std::endl;
  if (!tap_dir.empty()) {
    const auto& tap = session.Tap();
    Save(tap_dir + "/engine-input.s16", tap.input);
    Save(tap_dir + "/engine-output.s16", tap.output);
    std::ofstream info(tap_dir + "/tap.txt", std::ios::trunc);
    info << "input_frames=" << tap.input.size() / 2 << "\noutput_frames=" << tap.output.size() / 2
         << "\ndiscontinuities=" << tap.discontinuities.size() << "\nfallbacks=" << last.cleaner.fallbacks
         << "\nmode=" << settings.mode << "\nbackground_db=" << settings.optimized_background_db
         << "\nbuffering=" << settings.buffering << "\n";
  }
  return source_closed ? 3 : 0;
}
