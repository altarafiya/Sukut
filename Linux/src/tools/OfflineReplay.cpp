// Stage 3 parity: run the engine offline over the exact input the live session
// received, in the same packets (first 1536 frames, then 960), with the same
// settings. The output must equal what the live session produced.
//   sukut_offline_replay ENGINE_DIR INPUT.s16 OUTPUT.s16 MODE BACKGROUND_DB BUFFERING
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <vector>

#include "audio/SukutSession.h"
#include "live/AudioBuffering.h"

int main(int argc, char** argv) {
  using namespace cleanwave;
  if (argc != 7) {
    std::cerr << "usage: sukut_offline_replay ENGINE_DIR INPUT.s16 OUTPUT.s16 MODE BACKGROUND_DB BUFFERING\n";
    return 2;
  }
  const std::filesystem::path dir = argv[1];
  std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
  if (!in) { std::cerr << "cannot read " << argv[2] << '\n'; return 1; }
  std::vector<std::int16_t> input(static_cast<std::size_t>(in.tellg()) / 2);
  in.seekg(0);
  in.read(reinterpret_cast<char*>(input.data()), static_cast<std::streamsize>(input.size() * 2));

  const auto buffering = AudioBuffering::ForLevel(std::atoi(argv[6]));
  RealtimeCleaner cleaner(dir / "separator.tflite", dir / "window_576_q15.bin", dir / "window_960_q15.bin",
                          true, std::chrono::milliseconds(buffering.burst_ms), false);
  cleaner.EnableEfficiency(true);
  cleaner.Configure(sukut::linux_audio::SukutSession::MixFor(std::atoi(argv[4]), std::atoi(argv[5])), true);
  std::stop_source stop;
  if (!cleaner.Warmup(stop.get_token())) { std::cerr << "warmup failed\n"; return 1; }

  std::vector<std::int16_t> output;
  AudioLevel::PcmHop hop{};
  const std::size_t total = input.size() / 2;
  std::size_t at = 0;
  bool first = true;
  while (true) {
    const std::size_t frames = first ? StreamingDsp::kFirstInputFrames : StreamingDsp::kHopFrames;
    if (at + frames > total) break;
    cleaner.Process(std::span<const std::int16_t>(input.data() + at * 2, frames * 2), first, hop);
    output.insert(output.end(), hop.begin(), hop.end());
    at += frames;
    first = false;
  }
  std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size() * 2));
  const auto s = cleaner.Snapshot();
  std::cout << "REPLAYED input_frames=" << total << " output_frames=" << output.size() / 2
            << " fallbacks=" << s.fallbacks << " standby_entries=" << s.standby_entries << std::endl;
  return 0;
}
