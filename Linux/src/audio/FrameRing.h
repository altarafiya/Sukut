#pragma once

// Single-producer / single-consumer ring of interleaved stereo S16 frames.
// One side is PipeWire's real-time thread: no locks and no allocation after Reset.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace sukut::linux_audio {

class FrameRing {
 public:
  // Only while neither side is running.
  void Reset(std::uint32_t capacity_frames) {
    capacity_ = std::max<std::uint32_t>(capacity_frames, 1);
    buffer_.assign(static_cast<std::size_t>(capacity_) * 2, 0);
    written_ = 0;
    read_ = 0;
  }
  [[nodiscard]] std::uint32_t Capacity() const { return capacity_; }
  [[nodiscard]] std::uint32_t Fill() const {
    return static_cast<std::uint32_t>(written_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire));
  }
  // Absolute frame positions since Reset.
  [[nodiscard]] std::uint64_t WriteIndex() const { return written_.load(std::memory_order_acquire); }
  [[nodiscard]] std::uint64_t ReadIndex() const { return read_.load(std::memory_order_acquire); }

  // Producer: copies as many frames as fit; returns that count.
  std::uint32_t Write(std::span<const std::int16_t> source) {
    const std::uint64_t write = written_.load(std::memory_order_relaxed);
    const std::uint64_t space = capacity_ - (write - read_.load(std::memory_order_acquire));
    const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(source.size() / 2, space));
    for (std::uint32_t i = 0; i < frames; ++i) {
      const std::size_t at = static_cast<std::size_t>((write + i) % capacity_) * 2;
      buffer_[at] = source[i * 2];
      buffer_[at + 1] = source[i * 2 + 1];
    }
    written_.store(write + frames, std::memory_order_release);
    return frames;
  }

  // Consumer: copies as many frames as are available; returns that count.
  std::uint32_t Read(std::span<std::int16_t> target) {
    const std::uint64_t read = read_.load(std::memory_order_relaxed);
    const std::uint64_t fill = written_.load(std::memory_order_acquire) - read;
    const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(target.size() / 2, fill));
    for (std::uint32_t i = 0; i < frames; ++i) {
      const std::size_t at = static_cast<std::size_t>((read + i) % capacity_) * 2;
      target[i * 2] = buffer_[at];
      target[i * 2 + 1] = buffer_[at + 1];
    }
    read_.store(read + frames, std::memory_order_release);
    return frames;
  }

  // Consumer: drop everything buffered so far.
  void DropAll() { read_.store(written_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  std::vector<std::int16_t> buffer_;
  std::uint32_t capacity_ = 1;
  std::atomic<std::uint64_t> written_{0}, read_{0};
};

}  // namespace sukut::linux_audio
