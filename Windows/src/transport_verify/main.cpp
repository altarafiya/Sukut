#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include "live/LiveSession.h"

using namespace cleanwave;
using namespace std::chrono_literals;
namespace {
std::wstring Quote(const std::wstring& s) {
  if (s.find(L'"') != std::wstring::npos) throw std::invalid_argument("quote in argument");
  return L"\"" + s + L"\"";
}
struct Source {
  PROCESS_INFORMATION p{};
  Source(const std::filesystem::path& exe, const AudioRoute& route, const std::filesystem::path& report) {
    auto command = Quote(exe.wstring()) + L" " + Quote(route.source_render_id) + L" 48000 16 " + Quote(report.wstring());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &p)) throw std::runtime_error("source launch failed");
    CloseHandle(p.hThread);
  }
  ~Source() {
    if (WaitForSingleObject(p.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(p.hProcess, 1); WaitForSingleObject(p.hProcess, 1000);
    }
    CloseHandle(p.hProcess);
  }
  void Finish() {
    if (WaitForSingleObject(p.hProcess, 4000) != WAIT_OBJECT_0) throw std::runtime_error("source timeout");
    DWORD code = 1; GetExitCodeProcess(p.hProcess, &code);
    if (code) throw std::runtime_error("source failed");
  }
};
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 9 || argc > 12) { std::cerr << "usage: transport_verify model rise hop source-id capture-id output-id source-exe evidence-dir [--continuous] [--ordinary-worker]\n"; return 2; }
  try {
    bool continuous = false, scheduling = true, direct = false;
    for (int i = 9; i < argc; ++i) {
      if (std::wstring(argv[i]) == L"--continuous") continuous = true;
      else if (std::wstring(argv[i]) == L"--ordinary-worker") scheduling = false;
      else if (std::wstring(argv[i]) == L"--direct-playback") direct = true;
      else throw std::invalid_argument("unknown transport verification option");
    }
    const std::filesystem::path evidence(argv[8]); std::filesystem::create_directories(evidence);
    const AudioRoute route{argv[4], argv[5], argv[6]};
    LiveSession session(argv[1], argv[2], argv[3], route, scheduling, 0, true, {}, direct);
    session.Configure({0,-20,-20,100,0});
    session.SetMode(continuous ? ProcessingMode::Custom : ProcessingMode::Off); session.Start();
    Source source(argv[7], route, evidence/L"source.txt");
    std::ofstream out(evidence/L"transport.txt");
    const auto begin = std::chrono::steady_clock::now();
    std::uint64_t previous_empty = 0, previous_deficit = 0, previous_missing = 0;
    for (int phase = 0; phase < 4; ++phase) {
      const auto mode = continuous ? ProcessingMode::Custom : (phase % 2 ? ProcessingMode::Default : ProcessingMode::Off);
      session.SetMode(mode);
      const auto deadline = begin + std::chrono::seconds((phase + 1) * 4);
      do {
        std::this_thread::sleep_for(20ms);
        auto s = session.Snapshot();
        if (s.state == LiveState::Faulted) throw std::runtime_error(s.error);
      } while (std::chrono::steady_clock::now() < deadline);
      const auto s = session.Snapshot();
      WriteLiveStatus(evidence/(L"phase-" + std::to_wstring(phase) + L".json"), s);
      out << "phase=" << phase << " mode=" << ModeName(mode)
          << " empty_padding_delta=" << s.render.empty_render_observations - previous_empty
          << " clock_deficit_delta_frames=" << s.render.render_clock_deficit_frames - previous_deficit
          << " capture_missing_delta_frames=" << s.capture.capture_missing_frames - previous_missing
          << " capture_discontinuities=" << s.capture.discontinuities
          << " render_write_interval_max_ms=" << s.render.max_render_write_interval_ms
          << " capture_packet_interval_max_ms=" << s.capture.max_capture_packet_interval_ms
          << " queues_max=" << s.pipeline.captured_high_water << '/' << s.pipeline.rendered_high_water
          << " ai_p99_ms=" << s.pipeline.ai_engine.p99_ms << '\n';
      out << " ai_cpu_accounting_mean_ms=" << s.pipeline.ai_engine_cpu.mean_ms
          << " ai_elapsed_minus_cpu_mean_ms=" << s.pipeline.ai_engine_elapsed_minus_cpu_mean_ms
          << " fallbacks=" << s.cleaner.fallbacks << " recoveries=" << s.cleaner.recoveries
          << " bypasses=" << s.cleaner.bypasses << " ai_runs=" << s.cleaner.ai_runs
          << " schedule_requested=" << s.pipeline.processing_schedule.requested
          << " mmcss=" << s.pipeline.processing_schedule.mmcss_registered
          << " high_qos=" << s.pipeline.processing_schedule.high_qos << '\n';
      previous_empty = s.render.empty_render_observations;
      previous_deficit = s.render.render_clock_deficit_frames;
      previous_missing = s.capture.capture_missing_frames;
    }
    source.Finish(); const auto stop = std::chrono::steady_clock::now();
    if (!session.Stop()) throw std::runtime_error("shutdown failed");
    WriteLiveStatus(evidence/L"final.json", session.Snapshot());
    out << "stop_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-stop).count()
        << " transport_underrun_measurements_are_clock_estimates_no_acoustic_claim\nRESULT=PASS\n";
    std::cout << "bounded transport measurement completed: " << evidence << '\n'; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
