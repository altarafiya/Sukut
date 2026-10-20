#include "realtime/BufferedAudioPipeline.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cleanwave {
namespace {
constexpr auto kPoll = std::chrono::milliseconds(100);
using Clock = std::chrono::steady_clock;
template <typename Adapter>
struct AdapterCleanup {
  Adapter* adapter;
  ~AdapterCleanup() { if (adapter) adapter->Close(); }
};
}

void BufferedAudioPipeline::TimingAccumulator::Add(std::chrono::nanoseconds duration, bool retain_history) {
  const auto ns = duration.count();
  if (retain_history) {
    if (recent.empty()) recent.resize(4096);
    recent[count % recent.size()] = ns;
  }
  ++count;
  if (duration > RecoveryController::kBudget) ++over_budget;
  total_ns += ns;
  max_ns = std::max(max_ns, ns);
}

TimingSummary BufferedAudioPipeline::TimingAccumulator::Summarize(bool percentiles) const {
  TimingSummary result;
  result.count = count;
  result.over_budget = over_budget;
  result.max_ms = static_cast<double>(max_ns) / 1e6;
  if (count == 0) return result;
  result.mean_ms = static_cast<double>(total_ns / count) / 1e6;
  if (!percentiles || recent.empty()) return result;
  result.percentile_samples = static_cast<std::size_t>(std::min<std::uint64_t>(count, recent.size()));
  // Sorting occurs only for explicitly requested diagnostic snapshots.
  auto sorted = recent;
  std::sort(sorted.begin(), sorted.begin() + result.percentile_samples);
  const auto percentile = [&](double fraction) {
    const auto rank = static_cast<std::size_t>(std::ceil(result.percentile_samples * fraction));
    return static_cast<double>(sorted[std::max<std::size_t>(rank, 1) - 1]) / 1e6;
  };
  result.p50_ms = percentile(.50);
  result.p95_ms = percentile(.95);
  result.p99_ms = percentile(.99);
  return result;
}

BufferedAudioPipeline::BufferedAudioPipeline(
    std::shared_ptr<PacketProcessor> processor, std::shared_ptr<StreamInput> input,
    std::shared_ptr<StreamOutput> output, PipelineOptions options)
    : processor_(std::move(processor)), input_(std::move(input)),
      output_(std::move(output)), options_(options) {
  if (!processor_ || !input_ || !output_ || options.queue_timeout.count() <= 0 ||
      options.io_idle_timeout.count() <= 0) {
    throw std::invalid_argument("invalid realtime pipeline arguments");
  }
  output_packets_.resize(options_.direct_playback ? 1 : kPoolCapacity);
}

BufferedAudioPipeline::~BufferedAudioPipeline() {
  RequestStop();
  std::lock_guard lock(lifecycle_mutex_);
  // Adapters' cancellation contract is required for bounded destruction.
  JoinFinished();
}

void BufferedAudioPipeline::JoinFinished() {
  for (auto* thread : {&capture_thread_, &process_thread_, &playback_thread_}) {
    if (thread->joinable()) thread->join();
  }
}

void BufferedAudioPipeline::Launch(std::thread& thread,
    void (BufferedAudioPipeline::*work)(std::stop_token), std::stop_token stop) {
  ++live_workers_;
  try {
    thread = std::thread([this, work, stop] {
      try { (this->*work)(stop); }
      catch (const std::exception& e) { Fail(e.what()); }
      catch (...) { Fail("unknown worker failure"); }
      {
        std::lock_guard lock(status_mutex_);
        if (--live_workers_ == 0) active_.store(false);
      }
      finished_.notify_all();
    });
  } catch (...) {
    --live_workers_;
    throw;
  }
}

void BufferedAudioPipeline::Start() {
  std::lock_guard lifecycle(lifecycle_mutex_);
  if (live_workers_.load() != 0) throw std::logic_error("pipeline already running");
  if (input_->SampleRate() != 48000 || output_->SampleRate() != 48000) {
    throw std::invalid_argument("worker adapters must supply/consume 48000 Hz");
  }
  JoinFinished();
  std::stop_token stop;
  {
    std::lock_guard stop_lock(stop_mutex_);
    stop_source_ = std::stop_source{};
    stop = stop_source_.get_token();
    captured_.Reset(); rendered_.Reset(); free_inputs_.Reset(); free_outputs_.Reset();
    for (std::size_t i = 0; i < kPoolCapacity; ++i) {
      free_inputs_.Push(i, stop, kPoll);
      if (!options_.direct_playback) free_outputs_.Push(i, stop, kPoll);
    }
  }
  {
    std::lock_guard status_lock(status_mutex_);
    status_ = {};
    ai_engine_ = {}; ai_packet_ = {}; bypass_packet_ = {}; ai_engine_cpu_ = {};
    if (options_.collect_diagnostics)
      for (auto* timing : {&ai_engine_, &ai_packet_, &bypass_packet_, &ai_engine_cpu_}) timing->recent.resize(4096);
    process_ready_ = playback_ready_ = false;
    order_.store(0);
    active_.store(true);
  }
  try {
    Launch(process_thread_, &BufferedAudioPipeline::Process, stop);
    if (!options_.direct_playback) Launch(playback_thread_, &BufferedAudioPipeline::Playback, stop);
    {
      std::unique_lock lock(status_mutex_);
      ready_.wait(lock, [&] {
        return stop.stop_requested() || !status_.failure.empty() ||
               (process_ready_ && playback_ready_);
      });
    }
    if (!stop.stop_requested()) Launch(capture_thread_, &BufferedAudioPipeline::Capture, stop);
  } catch (const std::exception& e) {
    Fail(e.what());
    throw;
  } catch (...) {
    Fail("pipeline thread startup failed");
    throw;
  }
}

void BufferedAudioPipeline::RequestStop() {
  {
    std::lock_guard lock(stop_mutex_);
    stop_source_.request_stop();
    captured_.Close(); rendered_.Close(); free_inputs_.Close(); free_outputs_.Close();
    active_.store(false);
  }
  { std::lock_guard lock(status_mutex_); ready_.notify_all(); }
  idle_.notify_all();
}

void BufferedAudioPipeline::Fail(const char* message) {
  {
    std::lock_guard lock(status_mutex_);
    if (status_.failure.empty()) status_.failure = message;
  }
  RequestStop();
}

bool BufferedAudioPipeline::WaitStopped(std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  std::unique_lock lifecycle(lifecycle_mutex_, std::defer_lock);
  if (!lifecycle.try_lock_until(deadline)) return false;
  std::unique_lock status_lock(status_mutex_);
  if (!finished_.wait_until(status_lock, deadline, [&] { return live_workers_.load() == 0; })) {
    return false;
  }
  status_lock.unlock();
  JoinFinished();
  return true;
}

bool BufferedAudioPipeline::IdleWait(std::stop_token stop) {
  std::unique_lock lock(idle_mutex_);
  idle_.wait_for(lock, stop, std::chrono::milliseconds(2), [] { return false; });
  return !stop.stop_requested();
}

void BufferedAudioPipeline::Capture(std::stop_token stop) {
  AdapterCleanup<StreamInput> cleanup{input_.get()};
  input_->Open(stop);
  std::uint64_t sequence = 0;
  while (!stop.stop_requested()) {
    std::size_t slot = 0;
    auto result = free_inputs_.Pop(slot, stop, options_.queue_timeout);
    if (result == QueueResult::Cancelled || result == QueueResult::Closed) return;
    if (result != QueueResult::Success) throw std::runtime_error("input pool stalled");
    auto& packet = input_packets_[slot];
    packet.first = sequence == 0;
    packet.sequence = sequence;
    auto frames = packet.first ? StreamingDsp::kFirstInputFrames
                                     : StreamingDsp::kHopFrames;
    std::size_t offset = 0;
    auto last_progress = Clock::now();
    while (offset < frames && !stop.stop_requested()) {
      if (sequence == 0 && offset == 0) {
        std::lock_guard lock(status_mutex_);
        if (status_.capture_first_read_order == 0) status_.capture_first_read_order = ++order_;
      }
      const auto read = input_->Read(std::span(packet.samples).subspan(offset * 2, (frames - offset) * 2), stop);
      if (stop.stop_requested()) return;
      if (read.frames > frames - offset ||
          (read.status != ReadStatus::Data && read.frames != 0) ||
          (read.discontinuity && read.frames == 0)) {
        throw std::runtime_error("invalid capture frame count/status");
      }
      if (read.status == ReadStatus::End) {
        if (offset != 0) throw std::runtime_error("EOS inside continuous input packet");
        free_inputs_.Push(slot, stop, options_.queue_timeout);
        captured_.Close();
        return;
      }
      if (read.frames == 0) {
        { std::lock_guard lock(status_mutex_); ++status_.input_empty_reads; }
        if (!input_->AllowsIdleCapture() && Clock::now() - last_progress >= options_.io_idle_timeout) {
          throw std::runtime_error("capture stopped producing data");
        }
        if (!input_->WaitsWhenBlocked()) {
          { std::lock_guard lock(status_mutex_); ++status_.input_idle_waits; }
          if (!IdleWait(stop)) return;
        }
      } else {
        if (read.discontinuity) {
          {
            std::lock_guard lock(status_mutex_);
            ++status_.capture_segments_reset;
            status_.discarded_partial_frames += offset;
            if (sequence > 0) status_.discarded_future_context_frames += StreamingDsp::kOverlapFrames;
          }
          // Preserve only new-segment frames, then gather fresh 1536 context.
          std::copy_n(packet.samples.begin() + offset * 2, read.frames * 2, packet.samples.begin());
          offset = 0; packet.first = true; frames = StreamingDsp::kFirstInputFrames;
        }
        offset += read.frames;
        last_progress = Clock::now();
      }
    }
    if (stop.stop_requested()) return;
    result = captured_.Push(slot, stop, options_.queue_timeout);
    if (result == QueueResult::Cancelled || result == QueueResult::Closed) return;
    if (result != QueueResult::Success) throw std::runtime_error("capture queue saturated");
    { std::lock_guard lock(status_mutex_); ++status_.captured; }
    ++sequence;
  }
}

void BufferedAudioPipeline::Process(std::stop_token stop) {
  ProcessingThreadTask scheduling(options_.schedule_processing_audio);
  AdapterCleanup<StreamOutput> output_cleanup{options_.direct_playback ? output_.get() : nullptr};
  { std::lock_guard lock(status_mutex_); status_.processing_schedule = scheduling.Snapshot(); }
  if (!processor_->Warmup(stop)) {
    RequestStop();
    return;
  }
  if (options_.direct_playback) output_->Open(stop);
  {
    std::lock_guard lock(status_mutex_);
    process_ready_ = true;
    status_.process_ready_order = ++order_;
    if (options_.direct_playback) {
      playback_ready_ = true;
      status_.playback_ready_order = ++order_;
    }
  }
  ready_.notify_all();
  while (!stop.stop_requested()) {
    std::size_t input_slot = 0, output_slot = 0;
    const auto read = captured_.Pop(input_slot, stop, kPoll);
    if (read == QueueResult::Timeout) continue;
    if (read == QueueResult::Closed) {
      if (options_.direct_playback) output_->Finish(stop); else rendered_.Close();
      return;
    }
    if (read == QueueResult::Cancelled) return;
    auto result = options_.direct_playback ? QueueResult::Success : free_outputs_.Pop(output_slot, stop, options_.queue_timeout);
    if (result == QueueResult::Cancelled || result == QueueResult::Closed) return;
    if (result != QueueResult::Success) throw std::runtime_error("output pool stalled");
    const auto& input = input_packets_[input_slot];
    auto& output = output_packets_[output_slot];
    const auto count = (input.first ? StreamingDsp::kFirstInputFrames
                                    : StreamingDsp::kHopFrames) * 2;
    const auto started = Clock::now();
    const auto info = processor_->Process(std::span(input.samples).first(count), input.first, output.samples);
    const auto actual_packet_time = Clock::now() - started;
    output.sequence = input.sequence;
    {
      std::lock_guard lock(status_mutex_);
      ++status_.processed;
      if (info.work == CleanerWork::Bypass) bypass_packet_.Add(actual_packet_time,options_.collect_diagnostics);
      else {
        ai_engine_.Add(info.engine_time,options_.collect_diagnostics); ai_packet_.Add(actual_packet_time,options_.collect_diagnostics);
        if (info.engine_cpu_time_valid) {
          ai_engine_cpu_.Add(info.engine_cpu_time,options_.collect_diagnostics);
        }
      }
    }
    if (stop.stop_requested()) return;
    result = free_inputs_.Push(input_slot, stop, options_.queue_timeout);
    if (result != QueueResult::Success) {
      if (stop.stop_requested()) return;
      throw std::runtime_error("input pool return failed");
    }
    if (options_.direct_playback) {
      if (!WritePacket(output, stop)) return;
      continue;
    }
    result = rendered_.Push(output_slot, stop, options_.queue_timeout);
    if (result == QueueResult::Cancelled || result == QueueResult::Closed) return;
    if (result != QueueResult::Success) throw std::runtime_error("playback queue saturated");
  }
}

void BufferedAudioPipeline::Playback(std::stop_token stop) {
  AdapterCleanup<StreamOutput> cleanup{output_.get()};
  output_->Open(stop);
  {
    std::lock_guard lock(status_mutex_);
    playback_ready_ = true;
    status_.playback_ready_order = ++order_;
  }
  ready_.notify_all();
  while (!stop.stop_requested()) {
    std::size_t slot = 0;
    const auto read = rendered_.Pop(slot, stop, kPoll);
    if (read == QueueResult::Timeout) continue;
    if (read == QueueResult::Closed) { output_->Finish(stop); return; }
    if (read == QueueResult::Cancelled) return;
    const auto& packet = output_packets_[slot];
    if (!WritePacket(packet, stop)) return;
    const auto result = free_outputs_.Push(slot, stop, options_.queue_timeout);
    if (result != QueueResult::Success) {
      if (stop.stop_requested()) return;
      throw std::runtime_error("output pool return failed");
    }
  }
}

bool BufferedAudioPipeline::WritePacket(const OutputPacket& packet, std::stop_token stop) {
    std::size_t offset = 0;
    auto last_progress = Clock::now();
    while (offset < StreamingDsp::kHopFrames && !stop.stop_requested()) {
      const auto written = output_->Write(std::span(packet.samples).subspan(offset * 2),
          packet.sequence * StreamingDsp::kHopFrames + offset, stop);
      if (stop.stop_requested()) return false;
      if (written > StreamingDsp::kHopFrames - offset) {
        throw std::runtime_error("invalid playback frame count");
      }
      if (written == 0) {
        { std::lock_guard lock(status_mutex_); ++status_.output_empty_writes; }
        if (Clock::now() - last_progress >= options_.io_idle_timeout) {
          throw std::runtime_error("playback stopped consuming data");
        }
        if (!output_->WaitsWhenBlocked()) {
          { std::lock_guard lock(status_mutex_); ++status_.output_idle_waits; }
          if (!IdleWait(stop)) return false;
        }
      } else {
        offset += written;
        last_progress = Clock::now();
      }
    }
    if (stop.stop_requested()) return false;
    { std::lock_guard lock(status_mutex_); ++status_.played; }
    return true;
}

PipelineSnapshot BufferedAudioPipeline::Snapshot(bool include_timing) const {
  PipelineSnapshot result;
  if (include_timing) {
    TimingAccumulator engine, packet, bypass, cpu;
    {
      std::lock_guard lock(status_mutex_);
      result = status_;
      engine = ai_engine_; packet = ai_packet_; bypass = bypass_packet_;
      cpu = ai_engine_cpu_;
    }
    result.ai_engine = engine.Summarize(); result.ai_packet = packet.Summarize();
    result.bypass_packet = bypass.Summarize(); result.ai_engine_cpu = cpu.Summarize();
    if (result.ai_engine.count && result.ai_engine_cpu.count == result.ai_engine.count)
      result.ai_engine_elapsed_minus_cpu_mean_ms = result.ai_engine.mean_ms - result.ai_engine_cpu.mean_ms;
  } else {
    // Sukut consumes counters/faults, not percentile diagnostics. Do not copy
    // four 4096-entry histories or sort them every controller/UI status poll.
    std::lock_guard lock(status_mutex_); result = status_;
    result.ai_engine = ai_engine_.Summarize(false);
    result.ai_packet = ai_packet_.Summarize(false);
    result.bypass_packet = bypass_packet_.Summarize(false);
    result.ai_engine_cpu = ai_engine_cpu_.Summarize(false);
  }
  result.active = active_.load();
  result.captured_queue = captured_.Size(); result.rendered_queue = rendered_.Size();
  result.captured_high_water = captured_.HighWater(); result.rendered_high_water = rendered_.HighWater();
  result.free_inputs = free_inputs_.Size(); result.free_outputs = free_outputs_.Size();
  return result;
}

}  // namespace cleanwave
