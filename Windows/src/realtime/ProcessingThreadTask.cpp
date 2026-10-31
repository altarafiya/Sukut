#include "realtime/ProcessingThreadTask.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <avrt.h>

namespace cleanwave {
ProcessingThreadTask::ProcessingThreadTask(bool enabled) {
  snapshot_.requested = enabled;
  if (!enabled) return;
  DWORD index = 0;
  handle_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
  snapshot_.mmcss_registered = handle_ != nullptr;
  if (!handle_) snapshot_.mmcss_error = GetLastError();
  else {
    snapshot_.priority_set = AvSetMmThreadPriority(handle_, AVRT_PRIORITY_CRITICAL) != FALSE;
    if (!snapshot_.priority_set) snapshot_.priority_error = GetLastError();
  }
  THREAD_POWER_THROTTLING_STATE previous{}; previous.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
  if (GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &previous, sizeof(previous))) {
    previous_control_ = previous.ControlMask; previous_state_ = previous.StateMask; restore_qos_ = true;
  }
  THREAD_POWER_THROTTLING_STATE state{}; state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED; state.StateMask = 0;
  snapshot_.high_qos = SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state)) != FALSE;
  if (!snapshot_.high_qos) snapshot_.qos_error = GetLastError();
}
ProcessingThreadTask::~ProcessingThreadTask() {
  if (snapshot_.high_qos && restore_qos_) {
    THREAD_POWER_THROTTLING_STATE state{}; state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = previous_control_; state.StateMask = previous_state_;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
  }
  if (handle_) AvRevertMmThreadCharacteristics(handle_);
}
ThreadCpuSample ReadThreadCpuTime() noexcept {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return {};
  const auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return {(ticks(kernel) + ticks(user)) * 100, true};
}
} // namespace cleanwave

#else
// Linux: raise only this thread's nice value for processing. Unprivileged
// users are usually refused; the host's RealtimeKit request is tried then
// (Linux/PLAN.md, section 5.4), else the worker keeps normal priority.
#include <atomic>
#include <cerrno>
#include <ctime>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace cleanwave {
namespace {
constexpr int kProcessingNice = -11;
std::atomic<PriorityRequest> g_priority_request{nullptr};
id_t CurrentThreadId() { return static_cast<id_t>(::syscall(SYS_gettid)); }
}  // namespace

void SetPriorityRequest(PriorityRequest request) noexcept { g_priority_request = request; }

ProcessingThreadTask::ProcessingThreadTask(bool enabled) {
  snapshot_.requested = enabled;
  if (!enabled) return;
  errno = 0;
  const int previous = ::getpriority(PRIO_PROCESS, CurrentThreadId());
  if (errno != 0) { snapshot_.priority_error = static_cast<std::uint32_t>(errno); return; }
  previous_state_ = static_cast<std::uint32_t>(previous);
  snapshot_.priority_set = ::setpriority(PRIO_PROCESS, CurrentThreadId(), kProcessingNice) == 0;
  if (!snapshot_.priority_set) {
    snapshot_.priority_error = static_cast<std::uint32_t>(errno);
    const auto request = g_priority_request.load();
    snapshot_.priority_set = request != nullptr && request(static_cast<long>(CurrentThreadId()), kProcessingNice);
    if (snapshot_.priority_set) snapshot_.priority_error = 0;
  }
  if (snapshot_.priority_set) { snapshot_.relative_priority = kProcessingNice; restore_qos_ = true; }
}
ProcessingThreadTask::~ProcessingThreadTask() {
  if (restore_qos_) ::setpriority(PRIO_PROCESS, CurrentThreadId(), static_cast<int>(previous_state_));
}
ThreadCpuSample ReadThreadCpuTime() noexcept {
  timespec now{};
  if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return {};
  return {static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(now.tv_nsec), true};
}
} // namespace cleanwave
#endif
