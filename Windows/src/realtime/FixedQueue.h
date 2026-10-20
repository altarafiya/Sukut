#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace cleanwave {

enum class QueueResult { Success, Closed, Cancelled, Timeout };

// Fixed storage and cancellation-aware waits. Reset only when all workers joined.
template <typename T, std::size_t Capacity>
class FixedQueue final {
 public:
  QueueResult Push(T value, std::stop_token stop, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, stop, timeout, [&] { return closed_ || count_ < Capacity; })) {
      return stop.stop_requested() ? QueueResult::Cancelled : QueueResult::Timeout;
    }
    if (stop.stop_requested()) return QueueResult::Cancelled;
    if (closed_) return QueueResult::Closed;
    storage_[(head_ + count_) % Capacity] = value;
    ++count_;
    high_water_ = count_ > high_water_ ? count_ : high_water_;
    changed_.notify_all();
    return QueueResult::Success;
  }
  QueueResult Pop(T& value, std::stop_token stop, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, stop, timeout, [&] { return closed_ || count_ != 0; })) {
      return stop.stop_requested() ? QueueResult::Cancelled : QueueResult::Timeout;
    }
    if (stop.stop_requested()) return QueueResult::Cancelled;
    if (count_ == 0) return QueueResult::Closed;
    value = storage_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    changed_.notify_all();
    return QueueResult::Success;
  }
  void Close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    changed_.notify_all();
  }
  void Reset() {
    std::lock_guard lock(mutex_);
    head_ = count_ = high_water_ = 0;
    closed_ = false;
  }
  std::size_t Size() const { std::lock_guard lock(mutex_); return count_; }
  std::size_t HighWater() const { std::lock_guard lock(mutex_); return high_water_; }

 private:
  std::array<T, Capacity> storage_{};
  mutable std::mutex mutex_;
  std::condition_variable_any changed_;
  std::size_t head_ = 0, count_ = 0, high_water_ = 0;
  bool closed_ = false;
};

}  // namespace cleanwave
