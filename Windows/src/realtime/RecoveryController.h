#pragma once

#include <chrono>
#include <cstdint>

namespace cleanwave {

enum class CleanerWork { Run, Bypass, RecoveryProbe };

// Audio timing-only recovery. Live WASAPI can spend a finite part of its
// existing render reserve on brief overruns. Queue occupancy is not an input.
class RecoveryController final {
 public:
  static constexpr auto kBudget = std::chrono::milliseconds(20);
  static constexpr unsigned kCooldownHops = 13;  // ceil(250 / 20)
  static constexpr auto kLiveBurstAllowance = std::chrono::milliseconds(40);
  static constexpr auto kMaxLiveBurstAllowance = std::chrono::milliseconds(160);

  // Zero preserves the original strict Audio two-miss policy. The live
  // Lowest uses 40 ms of its 60 ms reserve. Higher profiles increase both
  // reserve and credit together, leaving at least one 20 ms hop as headroom.
  // Healthy run hops refill it; sustained overruns exhaust it.
  explicit RecoveryController(std::chrono::nanoseconds burst_allowance = {});

  CleanerWork NextWork();
  // True means neural state and synthesis tail must be reset after rendering.
  bool Complete(CleanerWork work, std::chrono::nanoseconds engine_time);
  void Reset();
  bool Paused() const { return paused_; }
  std::uint64_t FallbackEvents() const { return fallbacks_; }
  std::uint64_t Recoveries() const { return recoveries_; }
  std::uint64_t ProbeFailures() const { return probe_failures_; }
  std::chrono::nanoseconds BurstAllowance() const { return burst_allowance_; }
  std::chrono::nanoseconds BurstCredit() const { return burst_credit_; }
  std::uint64_t CoveredOverruns() const { return covered_overruns_; }

 private:
  bool paused_ = false;
  unsigned missed_ = 0;
  unsigned cooldown_ = 0;
  unsigned probe_backoff_ = kCooldownHops;
  unsigned healthy_ = 0;
  std::uint64_t fallbacks_ = 0, recoveries_ = 0, probe_failures_ = 0;
  std::chrono::nanoseconds burst_allowance_{}, burst_credit_{};
  std::uint64_t covered_overruns_ = 0;
};

}  // namespace cleanwave
