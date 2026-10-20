#pragma once
#include <cstdint>
#include <stdexcept>

namespace cleanwave {
// Bounded endpoint storage only. Model, hop, precision and pipeline pool sizes
// are identical in all profiles. Lowest preserves the existing values.
struct AudioBuffering {
  std::uint32_t capture_ms, render_ms, prefill_ms, burst_ms;
  static AudioBuffering ForLevel(int level) {
    switch (level) {
      case 0: return {100, 80, 60, 40};
      case 1: return {160, 140, 100, 80};
      case 2: return {240, 220, 180, 160};
      default: throw std::invalid_argument("invalid audio buffering level");
    }
  }
};
} // namespace cleanwave
