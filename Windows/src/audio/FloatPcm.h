#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace cleanwave {
inline void FloatToPcm16(std::span<const float> input, std::span<std::int16_t> output) {
  if (input.size() != output.size()) throw std::invalid_argument("float/PCM span mismatch");
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (!std::isfinite(input[i])) throw std::runtime_error("non-finite capture sample");
    output[i] = static_cast<std::int16_t>(std::clamp(input[i], -1.0F, 32767.0F / 32768.0F) * 32768.0F);
  }
}
inline void Pcm16ToFloat(std::span<const std::int16_t> input, std::span<float> output) {
  if (input.size() != output.size()) throw std::invalid_argument("PCM/float span mismatch");
  for (std::size_t i = 0; i < input.size(); ++i) output[i] = input[i] / 32768.0F;
}
}  // namespace cleanwave
