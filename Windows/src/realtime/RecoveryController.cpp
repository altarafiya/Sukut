#include "realtime/RecoveryController.h"

#include <algorithm>
#include <stdexcept>

namespace cleanwave {

RecoveryController::RecoveryController(std::chrono::nanoseconds burst_allowance)
    : burst_allowance_(burst_allowance), burst_credit_(burst_allowance) {
  if (burst_allowance < std::chrono::nanoseconds{} || burst_allowance > kMaxLiveBurstAllowance)
    throw std::invalid_argument("recovery burst allowance must fit the live render reserve");
}

CleanerWork RecoveryController::NextWork() {
  if (!paused_) return CleanerWork::Run;
  if (cooldown_ != 0) {
    --cooldown_;
    return CleanerWork::Bypass;
  }
  return CleanerWork::RecoveryProbe;
}

bool RecoveryController::Complete(CleanerWork work,
                                  std::chrono::nanoseconds engine_time) {
  if (work == CleanerWork::Bypass) return false;
  auto recovery_time = engine_time;
  if (work == CleanerWork::Run && burst_allowance_.count() != 0) {
    const auto excess = engine_time - kBudget;
    if (excess <= std::chrono::nanoseconds{}) {
      burst_credit_ = std::min(burst_allowance_, burst_credit_ - excess);
    } else {
      const auto covered = std::min(burst_credit_, excess);
      burst_credit_ -= covered;
      recovery_time -= covered;
      if (recovery_time <= kBudget) ++covered_overruns_;
    }
  }
  // Probes still have to meet the real 20 ms budget; no burst credit can
  // incorrectly declare sustained overload recovered. Raw timings are retained.
  const bool slow = recovery_time > kBudget;
  if (work == CleanerWork::RecoveryProbe) {
    if (slow) {
      healthy_ = 0;
      // Live recovery retries soon after a transient stall, then backs off if
      // the machine is still overloaded. No audio reserve or queue is added.
      probe_backoff_ = std::min(kCooldownHops, probe_backoff_ * 2);
      cooldown_ = probe_backoff_;
      ++probe_failures_;
      return true;
    }
    if (++healthy_ >= 2) {
      paused_ = false;
      missed_ = healthy_ = 0;
      burst_credit_ = burst_allowance_;
      ++recoveries_;
    }
    return false;
  }
  missed_ = slow ? missed_ + 1 : 0;
  if (missed_ < 2) return false;
  paused_ = true;
  missed_ = healthy_ = 0;
  probe_backoff_ = burst_allowance_.count() != 0 ? 2 : kCooldownHops;
  cooldown_ = probe_backoff_;
  ++fallbacks_;
  return true;
}

void RecoveryController::Reset() { *this = RecoveryController(burst_allowance_); }

}  // namespace cleanwave
