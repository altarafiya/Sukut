#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "dsp/OfflineCleaner.h"

namespace cleanwave {

// Finite-file extension to the live packet contract. Android defines no EOF
// flush. Missing future lookahead is zero-extended internally and the emitted
// result is trimmed to exactly the original number of frames.
std::vector<std::int16_t> ProcessOfflinePcm16(
    OfflineCleaner& cleaner,
    std::span<const std::int16_t> interleaved_stereo, bool run_model = true);

}  // namespace cleanwave
