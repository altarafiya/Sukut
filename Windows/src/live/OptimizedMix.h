#pragma once

#include <algorithm>

namespace cleanwave {

// Control-plane conversion only. The existing DSP consumes its cached gains.
struct OptimizedMix {
  bool advanced = false;
  int voice_db = 0, music_db = -20, background_db = 0;
};
struct ModeStemGains { int voice_db, music_db, background_db; };
inline ModeStemGains ModeGains(int mode, int background, OptimizedMix mix = {}) {
  if (mode == 0) return {0, 0, 0};
  if (mode == 2 && mix.advanced)
    return {std::clamp(mix.voice_db, -20, 10), std::clamp(mix.music_db, -20, 10), std::clamp(mix.background_db, -20, 10)};
  return {0, -100, mode == 3 ? -100 : mode == 2 ? std::clamp(background, -20, 10) : 0};
}

}  // namespace cleanwave
