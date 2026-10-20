#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include "realtime/BufferedAudioPipeline.h"

namespace cleanwave {
struct WasapiSnapshot {
  std::uint32_t client_rate = 0, mix_rate = 0, mix_channels = 0, buffer_frames = 0;
  double reported_stream_latency_ms = 0;
  std::uint64_t frames = 0, packets = 0, event_wakes = 0, wait_timeouts = 0;
  std::uint64_t startup_discontinuities = 0, discontinuities = 0, timestamp_errors = 0;
  std::uint64_t empty_render_observations = 0;
  std::uint64_t capture_position_gaps = 0, capture_missing_frames = 0;
  std::uint64_t render_clock_deficit_frames = 0, clock_read_errors = 0;
  double max_capture_packet_interval_ms = 0, max_render_write_interval_ms = 0;
  double max_capture_read_interval_ms = 0;
  std::uint32_t startup_prefill_frames = 0;
  bool high_resolution_timer = false;
  bool open = false;
  bool mmcss_registered = false;
  bool media_category_requested = false;
  bool high_qos = false, processing_thread_owns_schedule = false;
  std::uint32_t mmcss_error = 0;
};

// Open/Close and every I/O call must run on the same owning thread. Pipeline
// lifecycle hooks enforce that. Instances contain no COM state before Open.
// Rates other than 48k are for standalone conversion validation; the worker
// pipeline explicitly rejects adapters that do not expose 48k.
class WasapiCapture final : public StreamInput {
 public:
  explicit WasapiCapture(std::wstring endpoint_id, std::uint32_t rate = 48000,
                         bool resume_on_discontinuity = false, std::uint32_t buffer_ms = 100);
  ~WasapiCapture();
  std::uint32_t SampleRate() const override { return rate_; }
  bool WaitsWhenBlocked() const override { return true; }
  bool AllowsIdleCapture() const override { return resume_on_discontinuity_; }
  void Open(std::stop_token stop) override;
  ReadResult Read(std::span<std::int16_t> destination, std::stop_token stop) override;
  void Close() noexcept override;
  WasapiSnapshot Snapshot() const;
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::wstring endpoint_id_;
  std::uint32_t rate_;
  bool resume_on_discontinuity_;
  std::uint32_t buffer_ms_;
  mutable std::mutex mutex_;
  WasapiSnapshot snapshot_;
};

class WasapiRender final : public StreamOutput {
 public:
  explicit WasapiRender(std::wstring endpoint_id, std::uint32_t rate = 48000,
                        std::uint32_t buffer_ms = 80, std::uint32_t prefill_ms = 60,
                        std::shared_ptr<std::atomic<int>> amplification_db = {}, bool schedule_audio = true);
  ~WasapiRender();
  std::uint32_t SampleRate() const override { return rate_; }
  bool WaitsWhenBlocked() const override { return true; }
  void Open(std::stop_token stop) override;
  std::size_t Write(std::span<const std::int16_t> source, std::uint64_t first_frame,
                     std::stop_token stop) override;
  void Finish(std::stop_token stop) override;
  void Close() noexcept override;
  WasapiSnapshot Snapshot() const;
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::wstring endpoint_id_;
  std::uint32_t rate_;
  std::uint32_t buffer_ms_, prefill_ms_;
  std::shared_ptr<std::atomic<int>> amplification_db_;
  bool schedule_audio_ = true;
  mutable std::mutex mutex_;
  WasapiSnapshot snapshot_;
};
}  // namespace cleanwave
