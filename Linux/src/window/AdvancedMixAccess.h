#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>

#include "window/Sha256.h"

namespace sukut::window {
struct AdvancedMixAccess {
  int failures = 0;
  std::int64_t locked_until = 0;
  enum class Result { Enabled, Incorrect, Locked };
  // Only a salted SHA-256 of the access code is in the source.
  static bool IsAccessCode(std::string_view code) {
    return Sha256Hex(std::string("sukut-advanced-mix:").append(code)) == "1eab90bc571f33625f026854d109a8a8331d688bdddd31c1917f7631e49c6d60";
  }
  Result Attempt(std::string_view code, std::int64_t now,
                 const std::function<bool(std::string_view)>& matches = IsAccessCode) {
    if (locked_until > now) return Result::Locked;
    if (locked_until != 0) { failures = 0; locked_until = 0; }
    if (matches(code)) { failures = 0; return Result::Enabled; }
    failures = std::clamp(failures, 0, 2) + 1;
    if (failures == 3) { failures = 0; locked_until = now + 600000; return Result::Locked; }
    return Result::Incorrect;
  }
};
}  // namespace sukut::window
