// Verifies Sukut's Standby and >16 kHz guard on the real model, offline.
// Usage: sukut_efficiency_verify <model.tflite> <rise.bin> <hop.bin>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stop_token>
#include <vector>

#include "realtime/RealtimeCleaner.h"

using namespace cleanwave;

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
  if (!ok) ++failures;
}
}  // namespace

// Windows passes UTF-16 paths; Linux passes UTF-8. Both become std::filesystem::path.
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if (argc != 4) {
    std::cerr << "Usage: sukut_efficiency_verify <model.tflite> <rise.bin> <hop.bin>\n";
    return 2;
  }
  try {
    RealtimeCleaner cleaner(argv[1], argv[2], argv[3], true, std::chrono::milliseconds(40), false);
    cleaner.EnableEfficiency(true);
    cleaner.Configure({0, -100, 0, 100, 0}, true);  // Music Filter
    std::stop_source stop;
    if (!cleaner.Warmup(stop.get_token())) return 3;

    std::vector<std::int16_t> first(StreamingDsp::kFirstInputFrames * 2, 0);
    std::vector<std::int16_t> hop(StreamingDsp::kHopFrames * 2, 0);
    AudioLevel::PcmHop output{};
    cleaner.Process(first, true, output);
    std::uint64_t entered_at = 0;
    for (std::uint64_t i = 1; i <= 300; ++i) {
      cleaner.Process(hop, false, output);
      if (!entered_at && cleaner.Snapshot().standby) entered_at = i;
    }
    const auto resting = cleaner.Snapshot();
    Check(entered_at >= 250 && entered_at <= 252, "standby_after_5s_of_digital_silence");
    Check(resting.standby_packets >= 45, "standby_skips_model_work");
    Check(std::all_of(output.begin(), output.end(), [](std::int16_t s) { return s == 0; }), "standby_outputs_silence");
    const auto runs_before = resting.ai_runs;

    hop[hop.size() - 1] = 1;  // a single 1-LSB sample
    const auto info = cleaner.Process(hop, false, output);
    const auto woke = cleaner.Snapshot();
    Check(!woke.standby && info.work != CleanerWork::Bypass && woke.ai_runs == runs_before + 1,
          "one_lsb_resumes_processing_in_same_hop");

    // Strong broadband noise: the model runs and the guard reports a gain.
    std::uint32_t seed = 0x53554b55;
    for (int i = 0; i < 40; ++i) {
      for (auto& s : hop) { seed = seed * 1664525u + 1013904223u; s = static_cast<std::int16_t>(seed >> 18); }
      cleaner.Process(hop, false, output);
    }
    const auto busy = cleaner.Snapshot();
    Check(!busy.standby && busy.high_band_gain > 0.03F && busy.high_band_gain <= 1.0F, "high_band_gain_in_range");
    std::cout << "high_band_gain=" << busy.high_band_gain << " standby_entries=" << busy.standby_entries << '\n';
    std::cout << "kernels=" << busy.kernels << '\n';
  } catch (const std::exception& e) {
    std::cerr << "ERROR " << e.what() << '\n';
    return 1;
  }
  std::cout << "RESULT=" << (failures ? "FAIL" : "PASS") << '\n';
  return failures ? 1 : 0;
}
