#define NOMINMAX
#include <windows.h>
#include <avrt.h>
#include "realtime/ProcessingThreadTask.h"

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
