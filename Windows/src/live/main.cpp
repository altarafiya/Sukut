#include <windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "live/LiveSession.h"

using namespace cleanwave;
using namespace std::chrono_literals;
namespace {
std::atomic<bool> interrupted{false};
BOOL WINAPI Interrupt(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
    interrupted = true; return TRUE;
  }
  return FALSE;
}
bool Command(LiveSession& live, const std::string& text) {
  std::istringstream input(text); std::string word, extra;
  input >> word;
  if (word == "custom") {
    RealtimeMix mix;
    input >> std::ws;
    if (!input.eof()) {
      if (!(input >> mix.voice_db)) throw std::invalid_argument("invalid custom parameters");
      if (!(input >> mix.music_db >> mix.other_db >> mix.reduction_percent >> mix.boost_db))
        throw std::invalid_argument("custom expects five integers: voice music other reduction boost");
      if (input >> extra) throw std::invalid_argument("extra custom parameters");
      live.Configure(mix);
    } else live.SetMode(ProcessingMode::Custom);
  } else {
    if (input >> extra) throw std::invalid_argument("extra command parameters");
    if (word == "off") live.SetMode(ProcessingMode::Off);
    else if (word == "default") live.SetMode(ProcessingMode::Default);
    else if (word == "restart") live.Restart();
    else if (word == "stop") return false;
    else throw std::invalid_argument("unknown live command");
  }
  return true;
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 10) {
    std::cerr << "usage: cleanwave_live model rise hop source-render-id capture-id physical-render-id status.json commands.txt seconds(0=until_stop)\n";
    return 2;
  }
  try {
    const auto duration = std::chrono::seconds(std::stoul(argv[9]));
    const std::filesystem::path status(argv[7]), commands(argv[8]);
    LiveSession live(argv[1], argv[2], argv[3], {argv[4], argv[5], argv[6]});
    live.SetMode(ProcessingMode::Off);
    SetConsoleCtrlHandler(Interrupt, TRUE);
    std::cout << "CleanWave live: Off. Commands: off/default/custom [v m o reduction boost]/restart/stop\n"
              << "Command file: numbered complete lines, e.g. 1 default.\n"
              << "If an app uses CABLE Input, restore its prior output before stopping.\n";
    live.Start();
    const auto begin = std::chrono::steady_clock::now();
    auto next_status = begin;
    std::uint64_t command_id = 0;
    bool running = true;
    while (running && !interrupted && (duration == 0s || std::chrono::steady_clock::now() - begin < duration)) {
      std::string contents;
      {
        std::ifstream file(commands, std::ios::binary | std::ios::ate);
        if (file) {
          const auto bytes = file.tellg();
          if (bytes > 65536) throw std::runtime_error("command file exceeds 64 KiB; stop and start a fresh controller");
          file.seekg(0);
          contents.assign(std::istreambuf_iterator<char>(file), {});
        }
      } // Release the file before commands/status work or the polling wait.
      // Ignore the final incomplete line while another process is appending it.
      std::size_t first = 0, end;
      while ((end = contents.find('\n', first)) != std::string::npos) {
        std::istringstream line(contents.substr(first, end - first));
        first = end + 1;
        std::uint64_t id; std::string text;
        if (!(line >> id) || id <= command_id) continue;
        std::getline(line, text);
        command_id = id;
        try { running = Command(live, text); std::cout << "command " << id << ":" << text << '\n'; }
        catch (const std::exception& e) { std::cerr << "command " << id << " rejected: " << e.what() << '\n'; }
        if (!running) break;
      }
      const auto snapshot = live.Snapshot();
      if (snapshot.state == LiveState::Faulted) { std::cerr << "live failure: " << snapshot.error << '\n'; break; }
      if (std::chrono::steady_clock::now() >= next_status) {
        WriteLiveStatus(status, snapshot); next_status = std::chrono::steady_clock::now() + 1s;
      }
      std::this_thread::sleep_for(50ms);
    }
    const bool stopped = live.Stop();
    const auto final = live.Snapshot(); WriteLiveStatus(status, final);
    std::cout << "state=" << StateName(final.state) << " processed=" << final.pipeline.processed
              << " played=" << final.pipeline.played << " ai=" << final.cleaner.ai_runs
              << " off=" << final.cleaner.off_packets << " failure=" << final.error << '\n'
              << "Restore any source app still assigned to CABLE Input to its recorded prior output.\n";
    SetConsoleCtrlHandler(Interrupt, FALSE);
    return stopped && final.error.empty() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "live controller failure: " << e.what() << '\n'
              << "Restore any source app assigned to CABLE Input to its recorded prior output.\n";
    return 1;
  }
}
