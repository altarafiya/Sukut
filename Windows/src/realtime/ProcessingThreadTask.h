#pragma once
#include <chrono>
#include <cstdint>

namespace cleanwave {
struct ProcessingScheduleSnapshot {
  bool requested = false, mmcss_registered = false, priority_set = false, high_qos = false;
  std::uint32_t mmcss_error = 0, priority_error = 0, qos_error = 0;
  int relative_priority = 2; // AVRT_PRIORITY_CRITICAL; meaningful only when priority_set
};
// Calling-worker-only lifetime; affects this thread, never a process/system policy.
class ProcessingThreadTask final {
 public:
  explicit ProcessingThreadTask(bool enabled);
  ~ProcessingThreadTask();
  ProcessingThreadTask(const ProcessingThreadTask&) = delete;
  ProcessingScheduleSnapshot Snapshot() const { return snapshot_; }
 private:
  void* handle_ = nullptr;
  std::uint32_t previous_control_ = 0, previous_state_ = 0;
  bool restore_qos_ = false;
  ProcessingScheduleSnapshot snapshot_;
};
struct ThreadCpuSample { std::uint64_t nanoseconds = 0; bool valid = false; };
ThreadCpuSample ReadThreadCpuTime() noexcept;
} // namespace cleanwave
