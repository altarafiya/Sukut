#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "realtime/FixedQueue.h"
#include "realtime/PacketProcessor.h"
#include "realtime/ProcessingThreadTask.h"

namespace cleanwave {

enum class ReadStatus { Data, WouldBlock, End };
struct ReadResult {
  std::size_t frames = 0;
  ReadStatus status = ReadStatus::Data;
  bool discontinuity = false; // first data frames of a new contiguous segment
};

// Adapters MUST honor stop promptly, including interrupting blocked native I/O.
// Callbacks run on their dedicated worker; thrown errors stop the whole stream.
class StreamInput {
 public:
  virtual ~StreamInput() = default;
  virtual std::uint32_t SampleRate() const { return 48000; }
  // True only if every no-progress read already performs a cancellable wait.
  virtual bool WaitsWhenBlocked() const { return false; }
  // A shared virtual endpoint can legitimately have no packets while paused.
  // Device/API errors still throw; this only permits cancellable idle reads.
  virtual bool AllowsIdleCapture() const { return false; }
  virtual void Open(std::stop_token) {}
  virtual void Close() noexcept {}
  virtual ReadResult Read(std::span<std::int16_t> destination,
                          std::stop_token stop) = 0;
};
class StreamOutput {
 public:
  virtual ~StreamOutput() = default;
  virtual std::uint32_t SampleRate() const { return 48000; }
  // Avoid a second pipeline sleep after an adapter has waited for free space.
  virtual bool WaitsWhenBlocked() const { return false; }
  virtual void Open(std::stop_token) {}
  virtual void Finish(std::stop_token) {}
  virtual void Close() noexcept {}
  virtual std::size_t Write(std::span<const std::int16_t> source,
                             std::uint64_t first_frame,
                             std::stop_token stop) = 0;
};

struct TimingSummary {
  std::uint64_t count = 0, over_budget = 0;
  double mean_ms = 0, max_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0;
  std::size_t percentile_samples = 0;  // latest up to 4096 samples, exact rank
};
struct PipelineSnapshot {
  bool active = false;
  std::string failure;
  std::uint64_t captured = 0, processed = 0, played = 0;
  std::uint64_t input_empty_reads = 0, output_empty_writes = 0;
  std::uint64_t input_idle_waits = 0, output_idle_waits = 0;
  std::uint64_t capture_segments_reset = 0, discarded_partial_frames = 0;
  std::uint64_t discarded_future_context_frames = 0;
  std::size_t captured_queue = 0, rendered_queue = 0;
  std::size_t captured_high_water = 0, rendered_high_water = 0;
  std::size_t free_inputs = 0, free_outputs = 0;
  std::uint64_t process_ready_order = 0, playback_ready_order = 0;
  std::uint64_t capture_first_read_order = 0;
  TimingSummary ai_engine, ai_packet, bypass_packet;
  TimingSummary ai_engine_cpu;
  double ai_engine_elapsed_minus_cpu_mean_ms = 0; // coarse accounting comparison, not per-hop stall duration
  ProcessingScheduleSnapshot processing_schedule;
};

struct PipelineOptions {
  std::chrono::milliseconds queue_timeout{1000};
  std::chrono::milliseconds io_idle_timeout{200};
  bool schedule_processing_audio = false;
  bool collect_diagnostics = true;
  bool direct_playback = false; // inference/output share their owning worker
};

// Owns fixed pools (5 packets each) and bounded queues (3 each). No audio API.
// Input EOS drains complete packets. A partial packet at EOS is a stream error;
// finite WAV tail scheduling remains the explicit OfflinePipeline contract.
class BufferedAudioPipeline final {
 public:
  static constexpr std::size_t kQueueCapacity = 3, kPoolCapacity = 5;
  BufferedAudioPipeline(std::shared_ptr<PacketProcessor> processor,
                        std::shared_ptr<StreamInput> input,
                        std::shared_ptr<StreamOutput> output,
                        PipelineOptions options = {});
  ~BufferedAudioPipeline();
  BufferedAudioPipeline(const BufferedAudioPipeline&) = delete;
  BufferedAudioPipeline& operator=(const BufferedAudioPipeline&) = delete;

  // Synchronous readiness barrier: warmup/process + playback ready before capture.
  // RequestStop may run concurrently to cancel a start. Start/Wait serialize.
  void Start();
  void RequestStop();
  bool WaitStopped(std::chrono::milliseconds timeout);
  PipelineSnapshot Snapshot(bool include_timing = true) const;

 private:
  struct InputPacket {
    std::array<std::int16_t, StreamingDsp::kFirstInputFrames * 2> samples{};
    bool first = false;
    std::uint64_t sequence = 0;
  };
  struct OutputPacket {
    AudioLevel::PcmHop samples{};
    std::uint64_t sequence = 0;
  };
  struct TimingAccumulator {
    std::vector<std::int64_t> recent;
    std::uint64_t count = 0, over_budget = 0;
    long double total_ns = 0;
    std::int64_t max_ns = 0;
    void Add(std::chrono::nanoseconds duration, bool retain_history);
    TimingSummary Summarize(bool percentiles = true) const;
  };
  void Launch(std::thread& thread, void (BufferedAudioPipeline::*work)(std::stop_token),
              std::stop_token stop);
  void Capture(std::stop_token stop);
  void Process(std::stop_token stop);
  void Playback(std::stop_token stop);
  bool WritePacket(const OutputPacket& packet, std::stop_token stop);
  void Fail(const char* message);
  void JoinFinished();
  bool IdleWait(std::stop_token stop);
  std::shared_ptr<PacketProcessor> processor_;
  std::shared_ptr<StreamInput> input_;
  std::shared_ptr<StreamOutput> output_;
  PipelineOptions options_;
  std::array<InputPacket, kPoolCapacity> input_packets_{};
  std::vector<OutputPacket> output_packets_;
  FixedQueue<std::size_t, kQueueCapacity> captured_, rendered_;
  FixedQueue<std::size_t, kPoolCapacity> free_inputs_, free_outputs_;
  std::thread capture_thread_, process_thread_, playback_thread_;
  mutable std::timed_mutex lifecycle_mutex_;
  mutable std::mutex stop_mutex_, status_mutex_;
  std::mutex idle_mutex_;
  std::condition_variable ready_, finished_;
  std::condition_variable_any idle_;
  std::stop_source stop_source_;
  std::atomic<unsigned> live_workers_{0};
  std::atomic<bool> active_{false};
  bool process_ready_ = false, playback_ready_ = false;
  PipelineSnapshot status_;
  TimingAccumulator ai_engine_, ai_packet_, bypass_packet_;
  TimingAccumulator ai_engine_cpu_;
  std::atomic<std::uint64_t> order_{0};
};

}  // namespace cleanwave
