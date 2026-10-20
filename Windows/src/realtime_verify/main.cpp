#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "dsp/OfflineCleaner.h"
#include "realtime/BufferedAudioPipeline.h"
#include "realtime/RealtimeCleaner.h"

using namespace std::chrono_literals;
using namespace cleanwave;
using Clock = std::chrono::steady_clock;

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Action> void MustFail(Action action,const char* message) {
  bool failed=false;
  try{action();}catch(const std::exception&){failed=true;}
  Check(failed,message);
}
bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 2s) {
  const auto end = Clock::now() + timeout;
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(1ms);
  } while (Clock::now() < end);
  return predicate();
}
bool PauseUntil(std::stop_token stop, Clock::time_point deadline) {
  std::mutex mutex;
  std::condition_variable_any changed;
  std::unique_lock lock(mutex);
  changed.wait_until(lock, stop, deadline, [] { return false; });
  return !stop.stop_requested();
}
class Gate {
 public:
  bool Wait(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    return changed_.wait(lock, stop, [&] { return open_; });
  }
  void Open() { std::lock_guard lock(mutex_); open_ = true; changed_.notify_all(); }
 private:
  std::mutex mutex_;
  std::condition_variable_any changed_;
  bool open_ = false;
};

std::vector<std::int16_t> Fixture(std::size_t packets) {
  // Complete continuous packets: N output hops plus 576 future-context frames.
  const std::size_t frames = packets * 960 + 576;
  std::vector<std::int16_t> samples(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    samples[i * 2] = static_cast<std::int16_t>(7000 * std::sin(2 * 3.141592653589793 * 337 * t)
                                              + 2000 * std::sin(2 * 3.141592653589793 * 17333 * t));
    samples[i * 2 + 1] = static_cast<std::int16_t>(5000 * std::cos(2 * 3.141592653589793 * 719 * t)
                                                  - 1300 * std::sin(2 * 3.141592653589793 * 15891 * t));
  }
  return samples;
}

class MemoryInput final : public StreamInput {
 public:
  explicit MemoryInput(std::size_t packets) : samples(Fixture(packets)) {}
  bool WaitsWhenBlocked() const override { return waits_when_blocked; }
  ReadResult Read(std::span<std::int16_t> destination, std::stop_token stop) override {
    ++calls;
    if (gate && !gate->Wait(stop)) return {0, ReadStatus::WouldBlock};
    if (invalid) return {destination.size() / 2 + 1, ReadStatus::Data};
    if (idle || empty_calls > 0) {
      if (empty_calls > 0) --empty_calls;
      if (waits_when_blocked) PauseUntil(stop,Clock::now()+1ms);
      return {0, ReadStatus::WouldBlock};
    }
    if (cursor == samples.size()) return {0, ReadStatus::End};
    if (paced) {
      // Absolute deadlines prevent Windows wait rounding from accumulating
      // into a slower-than-48kHz source that would hide queue growth.
      next_read = cursor == 0 ? Clock::now() + 32ms : next_read + 20ms;
      if (!PauseUntil(stop, next_read)) return {0, ReadStatus::WouldBlock};
    }
    const auto frames = std::min({destination.size() / 2, max_frames, (samples.size() - cursor) / 2});
    std::copy_n(samples.begin() + cursor, frames * 2, destination.begin());
    cursor += frames * 2;
    return {frames, ReadStatus::Data};
  }
  std::vector<std::int16_t> samples;
  std::size_t cursor = 0, max_frames = 3072;
  unsigned empty_calls = 0;
  bool idle = false, invalid = false, paced = false, waits_when_blocked = false;
  std::shared_ptr<Gate> gate;
  std::atomic<unsigned> calls{0};
  Clock::time_point next_read{};
};

class MemoryOutput final : public StreamOutput {
 public:
  explicit MemoryOutput(std::size_t packets) { samples.reserve(packets * 1920); }
  bool WaitsWhenBlocked() const override { return waits_when_blocked; }
  std::size_t Write(std::span<const std::int16_t> source, std::uint64_t first_frame,
                    std::stop_token stop) override {
    ++calls;
    if (gate && !gate->Wait(stop)) return 0;
    if (invalid) return source.size() / 2 + 1;
    if (idle || empty_calls > 0) {
      if (empty_calls > 0) --empty_calls;
      if (waits_when_blocked) PauseUntil(stop,Clock::now()+1ms);
      return 0;
    }
    Check(first_frame == samples.size() / 2, "output frame timeline discontinuity");
    const auto frames = std::min(max_frames, source.size() / 2);
    samples.insert(samples.end(), source.begin(), source.begin() + frames * 2);
    return frames;
  }
  std::vector<std::int16_t> samples;
  std::size_t max_frames = 960;
  unsigned empty_calls = 0;
  bool idle = false, invalid = false, waits_when_blocked = false;
  std::shared_ptr<Gate> gate;
  std::atomic<unsigned> calls{0};
};

class FakeProcessor final : public PacketProcessor {
 public:
  bool Warmup(std::stop_token stop) override {
    warm_started = true;
    if (warm_gate && !warm_gate->Wait(stop)) return false;
    if (warm_fail) throw std::runtime_error("deliberate warmup failure");
    history.fill(0);
    count = 0;
    return !stop.stop_requested();
  }
  ProcessInfo Process(std::span<const std::int16_t> input, bool first,
                      AudioLevel::PcmHop& output) override {
    const auto start = Clock::now();
    if (++count == fail_on) throw std::runtime_error("deliberate process failure");
    if (delay.count() > 0) std::this_thread::sleep_for(delay);
    if (first) std::copy_n(input.begin(), output.size(), output.begin());
    else {
      std::copy(history.begin(), history.end(), output.begin());
      std::copy_n(input.begin(), output.size() - history.size(), output.begin() + history.size());
    }
    std::copy(input.end() - history.size(), input.end(), history.begin());
    ProcessInfo info;
    info.engine_time = info.total_time = Clock::now() - start;
    return info;
  }
  std::array<std::int16_t, 576 * 2> history{};
  std::atomic<unsigned> count{0};
  std::atomic<bool> warm_started{false};
  unsigned fail_on = 0;
  bool warm_fail = false;
  std::chrono::milliseconds delay{0};
  std::shared_ptr<Gate> warm_gate;
};

void AssertDrained(const PipelineSnapshot& s, std::size_t packets) {
  Check(!s.active && s.failure.empty(), "normal pipeline failure");
  Check(s.captured == packets && s.processed == packets && s.played == packets,
        "packets lost or repeated");
  Check(s.captured_queue == 0 && s.rendered_queue == 0, "queues not drained");
  Check(s.free_inputs == 5 && s.free_outputs == 5, "pooled buffers not returned");
  Check(s.captured_high_water <= 3 && s.rendered_high_water <= 3, "unbounded queue growth");
  Check(s.process_ready_order > 0 && s.playback_ready_order > 0 &&
        s.capture_first_read_order > s.process_ready_order &&
        s.capture_first_read_order > s.playback_ready_order, "consumer-first barrier violated");
}

void TestRecovery(std::ostringstream& report) {
  RecoveryController controller;
  for (int i = 0; i < 3; ++i) {
    Check(controller.NextWork() == CleanerWork::Run, "initial recovery work");
    Check(!controller.Complete(CleanerWork::Run, 20ms), "exact budget must be healthy");
  }
  Check(!controller.Complete(CleanerWork::Run, 21ms), "first miss should remain wet");
  Check(!controller.Complete(CleanerWork::Run, 5ms), "healthy hop must clear miss streak");
  Check(!controller.Complete(CleanerWork::Run, 21ms), "first consecutive miss");
  Check(controller.Complete(CleanerWork::Run, 21ms) && controller.Paused(), "second miss must reset/fallback");
  for (unsigned i = 0; i < 13; ++i) {
    Check(controller.NextWork() == CleanerWork::Bypass, "250ms cooldown geometry");
    Check(!controller.Complete(CleanerWork::Bypass, 50ms), "bypass must not count as AI");
  }
  Check(controller.NextWork() == CleanerWork::RecoveryProbe, "recovery probe selection");
  Check(controller.Complete(CleanerWork::RecoveryProbe, 21ms), "failed probe must reset");
  for (unsigned i = 0; i < 13; ++i) Check(controller.NextWork() == CleanerWork::Bypass, "failed probe cooldown");
  Check(controller.NextWork() == CleanerWork::RecoveryProbe, "first healthy probe");
  controller.Complete(CleanerWork::RecoveryProbe, 5ms);
  Check(controller.Paused(), "single probe prematurely unpaused");
  Check(controller.NextWork() == CleanerWork::RecoveryProbe, "second healthy probe");
  controller.Complete(CleanerWork::RecoveryProbe, 5ms);
  Check(!controller.Paused() && controller.Recoveries() == 1 &&
        controller.FallbackEvents() == 1 && controller.ProbeFailures() == 1,
        "recovery counters");
  controller.Reset();
  Check(!controller.Paused() && controller.FallbackEvents() == 0, "stream recovery reset");
  report << "recovery_exact_budget_two_misses_13hop_cooldown_failed_probe_two_healthy=PASS\n";

  // Independent expected PCM ramp for a synthetic dry=0.25, wet=0.10 signal.
  // These are simulated engine durations, never reported as measured model timing.
  AudioLevel level;
  StreamingDsp::HopStereo dry{}, wet{};
  dry.fill(.25F); wet.fill(.10F);
  AudioLevel::PcmHop pcm;
  level.ResetAmount();
  controller.Complete(CleanerWork::Run, 25ms);
  Check(controller.Complete(CleanerWork::Run, 25ms), "fade setup");
  level.Render(dry, wet, pcm, controller.Paused());
  Check(pcm[0] < pcm[2 * 249] && pcm[2 * 300] == 8192 && pcm.back() == 8192,
        "fallback fade must reach aligned original within one hop");
  Check(pcm[0] == pcm[1], "fade stereo order");
  for (unsigned i = 0; i < 13; ++i) {
    const auto work = controller.NextWork();
    Check(work == CleanerWork::Bypass, "fade cooldown");
    level.Render(dry, dry, pcm, true);
    Check(std::all_of(pcm.begin(), pcm.end(), [](auto x) { return x == 8192; }), "bypass PCM changed");
  }
  controller.Complete(controller.NextWork(), 5ms);
  level.Render(dry, wet, pcm, controller.Paused());
  Check(pcm.front() == 8192 && pcm.back() == 8192, "first probe must remain muted");
  controller.Complete(controller.NextWork(), 5ms);
  level.Render(dry, wet, pcm, controller.Paused());
  Check(pcm.front() > pcm.back() && pcm[0] == pcm[1], "healthy recovery fade");
  report << "aligned_dry_fallback_muted_probe_and_stereo_recovery_fade=PASS\n";

  RecoveryController buffered(40ms);
  // A 40 ms burst fits the existing startup reserve: keep wet output and state.
  Check(!buffered.Complete(CleanerWork::Run,40ms) && !buffered.Complete(CleanerWork::Run,40ms) &&
        !buffered.Paused() && buffered.BurstCredit()==0ms && buffered.CoveredOverruns()==2,
        "brief buffered overrun unexpectedly entered original-audio recovery");
  AudioLevel buffered_level;buffered_level.ResetAmount();
  buffered_level.Render(dry,wet,pcm,buffered.Paused());
  Check(pcm.front()<8192 && pcm.back()<8192,"buffered overrun leaked original PCM");
  // Once reserve is spent, sustained overload retains the original two-miss
  // fallback and strict healthy probes. No queue-size-dependent decision.
  Check(!buffered.Complete(CleanerWork::Run,21ms) && buffered.Complete(CleanerWork::Run,21ms) &&
        buffered.Paused(),"sustained buffered overload did not recover safely");
  for(unsigned i=0;i<2;++i)Check(buffered.NextWork()==CleanerWork::Bypass,"live first retry must be 40 ms");
  Check(buffered.Complete(buffered.NextWork(),21ms),"burst credit hid an unhealthy recovery probe");
  for(unsigned i=0;i<4;++i)Check(buffered.NextWork()==CleanerWork::Bypass,"failed live retry must back off to 80 ms");
  buffered.Complete(buffered.NextWork(),13ms);buffered.Complete(buffered.NextWork(),13ms);
  Check(!buffered.Paused() && buffered.BurstCredit()==40ms && buffered.Recoveries()==1,
        "buffered healthy recovery did not restore the bounded reserve");
  buffered.Reset();
  for(unsigned i=0;i<8;++i)Check(!buffered.Complete(CleanerWork::Run,25ms),"40 ms reserve depleted early");
  Check(!buffered.Complete(CleanerWork::Run,25ms) && buffered.Complete(CleanerWork::Run,25ms),
        "sustained 25 ms processing was incorrectly treated as real-time");
  buffered.Reset();buffered.Complete(CleanerWork::Run,50ms);
  Check(buffered.BurstCredit()==10ms,"burst reserve did not track the real overrun");
  buffered.Complete(CleanerWork::Run,10ms);Check(buffered.BurstCredit()==20ms,"healthy hop failed to refill reserve");
  buffered.Complete(CleanerWork::Run,0ms);buffered.Complete(CleanerWork::Run,0ms);
  Check(buffered.BurstCredit()==40ms,"burst reserve grew beyond the render headroom");
  MustFail([]{RecoveryController invalid(161ms);},"recovery accepted more credit than render reserve");
  report << "buffered_live_recovery=PASS two_40ms_spikes_keep_wet_state finite_40ms_credit healthy_refill_capped sustained_overload_fallback strict_20ms_probes reset_preserves_policy\n";
}

void TestWorkers(std::ostringstream& report) {
  constexpr std::size_t packets = 40;
  auto processor = std::make_shared<FakeProcessor>();
  auto input = std::make_shared<MemoryInput>(packets);
  auto output = std::make_shared<MemoryOutput>(packets);
  input->max_frames = 173; output->max_frames = 127;
  input->empty_calls = output->empty_calls = 3;
  auto pipeline = std::make_unique<BufferedAudioPipeline>(processor, input, output);
  pipeline->Start();
  Check(pipeline->WaitStopped(3s), "partial-I/O pipeline timeout");
  const auto snapshot = pipeline->Snapshot();
  AssertDrained(snapshot, packets);
  const auto counters = pipeline->Snapshot(false);
  Check(counters.captured == snapshot.captured && counters.processed == snapshot.processed &&
        counters.played == snapshot.played && counters.failure == snapshot.failure &&
        counters.free_inputs == snapshot.free_inputs && counters.free_outputs == snapshot.free_outputs &&
        counters.active == snapshot.active, "lightweight status lost counters/faults/pool state");
  Check(counters.ai_engine.count == snapshot.ai_engine.count && counters.ai_packet.count == snapshot.ai_packet.count &&
        counters.ai_engine.percentile_samples == 0, "lightweight status must expose aggregates without histories");
  report << "lightweight_status=PASS counters_faults_pools_preserved timing_histories_omitted\n";
  const auto measure_snapshots=[&](bool timing){
    std::uint64_t sum=0;const auto begin=Clock::now();
    for(int repeat=0;repeat<2000;++repeat)sum+=pipeline->Snapshot(timing).processed;
    Check(sum==packets*2000,"snapshot benchmark lost processing counters");
    return std::chrono::duration<double,std::micro>(Clock::now()-begin).count()/2000;
  };
  const auto full_status_us=measure_snapshots(true),light_status_us=measure_snapshots(false);
  report << "status_poll_microbenchmark full_us=" << full_status_us << " counters_only_us=" << light_status_us
         << " note=controller_work_not_acoustic_latency\n";
  Check(snapshot.input_empty_reads == 3 && snapshot.output_empty_writes == 3,
        "I/O stall counts");
  Check(snapshot.input_idle_waits == 3 && snapshot.output_idle_waits == 3,
        "nonblocking adapters must retain pipeline backoff");
  Check(output->samples.size() == packets * 1920 &&
        std::equal(output->samples.begin(), output->samples.end(), input->samples.begin()),
        "partial I/O lost alignment/stereo/PCM");
  report << "partial_IO_zero_progress_timeline_pool_return_consumer_first=PASS\n";

  // Same pipeline can restart only after prior workers join; all pools/state reset.
  for (int repeat = 0; repeat < 12; ++repeat) {
    input->cursor = 0; output->samples.clear();
    pipeline->Start(); Check(pipeline->WaitStopped(2s), "restart timeout");
    AssertDrained(pipeline->Snapshot(), packets);
    Check(std::equal(output->samples.begin(), output->samples.end(), input->samples.begin()), "restart state leakage");
  }
  report << "repeat_start_drain_join_restart_12_streams=PASS\n";

  input->cursor=0;output->samples.clear();input->empty_calls=output->empty_calls=3;
  input->waits_when_blocked=output->waits_when_blocked=true;
  pipeline->Start();Check(pipeline->WaitStopped(2s),"waiting adapters failed to drain");
  const auto waited=pipeline->Snapshot();AssertDrained(waited,packets);
  Check(waited.input_empty_reads==3 && waited.output_empty_writes==3 &&
        waited.input_idle_waits==0 && waited.output_idle_waits==0,"duplicate backoff after adapter wait");
  Check(std::equal(output->samples.begin(),output->samples.end(),input->samples.begin()),"waiting adapter changed PCM");
  report << "adapter_owned_wait=PASS no_duplicate_2ms_backoff PCM_timeline_exact nonblocking_backoff_preserved\n";

  input = std::make_shared<MemoryInput>(40);
  output = std::make_shared<MemoryOutput>(40);
  output->gate = std::make_shared<Gate>();
  processor = std::make_shared<FakeProcessor>();
  pipeline = std::make_unique<BufferedAudioPipeline>(processor, input, output);
  pipeline->Start();
  Check(WaitUntil([&] { return output->calls.load() > 0 && processor->count.load() >= 4; }),
        "processing stopped behind blocked playback");
  auto blocked = pipeline->Snapshot();
  Check(blocked.captured_high_water <= 3 && blocked.rendered_high_water <= 3 &&
        blocked.processed >= 4 && blocked.failure.empty(), "blocked playback queues");
  output->gate->Open();
  Check(pipeline->WaitStopped(2s), "released playback did not drain");
  AssertDrained(pipeline->Snapshot(), 40);
  report << "blocked_playback_independent_capture_process_and_bounded_queues=PASS\n";

  // Saturation fails explicitly within the configured bound and wakes all I/O.
  input = std::make_shared<MemoryInput>(60);
  output = std::make_shared<MemoryOutput>(60);
  output->gate = std::make_shared<Gate>();
  pipeline = std::make_unique<BufferedAudioPipeline>(std::make_shared<FakeProcessor>(), input, output,
                                                  PipelineOptions{80ms, 40ms});
  pipeline->Start(); Check(pipeline->WaitStopped(2s), "saturation did not stop workers");
  auto failed = pipeline->Snapshot();
  Check(!failed.failure.empty() && !failed.active && failed.captured_high_water <= 3 &&
        failed.rendered_high_water <= 3, "saturation failure absent");
  report << "sustained_saturation_bounded_failure=" << failed.failure << " PASS\n";

  // Temporary processing stalls grow only bounded queues; occupancy is not overload.
  processor = std::make_shared<FakeProcessor>(); processor->delay = 25ms;
  input = std::make_shared<MemoryInput>(8); output = std::make_shared<MemoryOutput>(8);
  pipeline = std::make_unique<BufferedAudioPipeline>(processor, input, output);
  pipeline->Start(); Check(pipeline->WaitStopped(2s), "slow processing timeout");
  AssertDrained(pipeline->Snapshot(), 8);
  Check(pipeline->Snapshot().ai_engine.over_budget == 8, "slow run accounting");
  report << "slow_processing_bounded_queues_measured_budget_misses=PASS\n";
}

void TestDirectPlayback(std::ostringstream& report) {
  constexpr std::size_t packets = 40;
  auto input = std::make_shared<MemoryInput>(packets);
  auto output = std::make_shared<MemoryOutput>(packets);
  auto processor = std::make_shared<FakeProcessor>();
  input->max_frames = 73; input->empty_calls = 3;
  output->max_frames = 37; output->empty_calls = 2;
  PipelineOptions options; options.direct_playback = true;
  BufferedAudioPipeline pipeline(processor, input, output, options);
  for (int pass = 0; pass != 2; ++pass) {
    input->cursor = 0; output->samples.clear();
    pipeline.Start(); Check(pipeline.WaitStopped(3s), "direct playback did not drain/restart");
    const auto state = pipeline.Snapshot(false);
    Check(state.failure.empty() && state.played == packets && state.processed == packets, "direct playback lost packets");
    Check(output->samples.size() == packets * 1920 && std::equal(output->samples.begin(), output->samples.end(), input->samples.begin()), "direct playback changed PCM/timeline");
    Check(state.rendered_high_water == 0 && state.free_outputs == 0, "direct playback retained the rendered queue");
    Check(state.ai_engine.count == packets && state.ai_engine.percentile_samples == 0, "cheap health summaries invalid");
  }
  report << "direct_playback: partial/empty I/O, exact PCM/timeline, drain/restart, aggregate health PASS\n";
}

void TestFailuresAndCancellation(std::ostringstream& report) {
  for (int mode = 0; mode < 7; ++mode) {
    auto processor = std::make_shared<FakeProcessor>();
    auto input = std::make_shared<MemoryInput>(8);
    auto output = std::make_shared<MemoryOutput>(8);
    if (mode == 0) input->idle = true;
    if (mode == 1) output->idle = true;
    if (mode == 2) input->invalid = true;
    if (mode == 3) output->invalid = true;
    if (mode == 4) processor->fail_on = 4;
    if (mode == 5) processor->warm_fail = true;
    if (mode == 6) input->samples.resize(200);  // deliberate partial first packet
    auto pipeline = std::make_unique<BufferedAudioPipeline>(processor, input, output,
                                                          PipelineOptions{200ms, 40ms});
    pipeline->Start(); Check(pipeline->WaitStopped(2s), "failure cleanup timeout");
    Check(!pipeline->Snapshot().failure.empty() && !pipeline->Snapshot().active,
          "worker error was not retained");
    report << "failure_case_" << mode << '=' << pipeline->Snapshot().failure << " PASS\n";
  }
  // Cancel during model/FFT warmup: capture must never run or publish data.
  auto processor = std::make_shared<FakeProcessor>();
  processor->warm_gate = std::make_shared<Gate>();
  auto input = std::make_shared<MemoryInput>(8);
  auto output = std::make_shared<MemoryOutput>(8);
  auto pipeline = std::make_unique<BufferedAudioPipeline>(processor, input, output);
  std::exception_ptr startup_error;
  std::thread starter([&] { try { pipeline->Start(); } catch (...) { startup_error = std::current_exception(); } });
  const bool warm_started = WaitUntil([&] { return processor->warm_started.load(); });
  const bool startup_wait_timed_out = !pipeline->WaitStopped(10ms);
  const auto cancel_start = Clock::now();
  pipeline->RequestStop();
  starter.join();
  if (startup_error) std::rethrow_exception(startup_error);
  Check(warm_started, "warmup did not begin");
  Check(startup_wait_timed_out, "startup wait ignored timeout");
  Check(pipeline->WaitStopped(500ms), "cancelled warmup did not stop");
  Check(input->calls.load() == 0 && output->calls.load() == 0 && pipeline->Snapshot().failure.empty(),
        "cancelled start touched stream");
  const double warm_cancel_ms = std::chrono::duration<double, std::milli>(Clock::now() - cancel_start).count();
  report << "cancel_during_warmup_no_capture=PASS elapsed_ms=" << warm_cancel_ms << "\n";

  // Blocked read/write cancellation plus 20 rapid lifecycle cycles.
  double max_cancel_ms = 0;
  for (int i = 0; i < 20; ++i) {
    input = std::make_shared<MemoryInput>(30);
    output = std::make_shared<MemoryOutput>(30);
    if (i % 2 == 0) input->gate = std::make_shared<Gate>();
    else output->gate = std::make_shared<Gate>();
    pipeline = std::make_unique<BufferedAudioPipeline>(std::make_shared<FakeProcessor>(), input, output);
    pipeline->Start();
    Check(WaitUntil([&] { return i % 2 == 0 ? input->calls.load() > 0 : output->calls.load() > 0; }),
          "blocked I/O did not begin");
    const auto start = Clock::now();
    pipeline->RequestStop();
    Check(pipeline->WaitStopped(500ms), "I/O/queue/pool cancellation timeout");
    max_cancel_ms = std::max(max_cancel_ms,
        std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    Check(pipeline->Snapshot().failure.empty(), "normal stop reported as failure");
  }
  report << "cancel_blocked_IO_queues_pools_20_cycles=PASS max_ms=" << max_cancel_ms << "\n";
}

class RecordingProcessor final : public PacketProcessor {
 public:
  RecordingProcessor(std::shared_ptr<RealtimeCleaner> cleaner, std::size_t capacity)
      : cleaner_(std::move(cleaner)) { records.reserve(capacity); }
  bool Warmup(std::stop_token stop) override { records.clear(); return cleaner_->Warmup(stop); }
  ProcessInfo Process(std::span<const std::int16_t> input, bool first, AudioLevel::PcmHop& output) override {
    auto info = cleaner_->Process(input, first, output);
    records.push_back(info);
    return info;
  }
  std::vector<ProcessInfo> records;
 private:
  std::shared_ptr<RealtimeCleaner> cleaner_;
};

void PrintTiming(std::ostringstream& report, const char* name, const TimingSummary& t) {
  report << name << " count=" << t.count << " mean_ms=" << t.mean_ms << " p50_ms=" << t.p50_ms
         << " p95_ms=" << t.p95_ms << " p99_ms=" << t.p99_ms << " max_ms=" << t.max_ms
         << " over_20ms=" << t.over_budget << " percentile_samples=" << t.percentile_samples << '\n';
}

void TestRealModel(const std::filesystem::path& model, const std::filesystem::path& rise,
                   const std::filesystem::path& hop, const std::filesystem::path& evidence,
                   std::ostringstream& report) {
  auto cleaner = std::make_shared<RealtimeCleaner>(model, rise, hop);
  const auto fixture = Fixture(4);
  Check(cleaner->Warmup({}), "real warmup failed");
  auto state = std::make_unique<SeparationModel::StateBuffer>(cleaner->ModelStateSnapshot());
  Check(std::all_of(state->begin(), state->end(), [](float v) { return v == 0; }), "warmup state not zero");
  AudioLevel::PcmHop realtime, offline, replay;
  cleaner->Process(std::span(fixture).first(3072), true, realtime);
  auto offline_cleaner = std::make_unique<OfflineCleaner>(model, rise, hop);
  offline_cleaner->ProcessPcm16(std::span(fixture).first(3072), true, offline);
  Check(realtime == offline, "warmup/reset changed first real packet PCM");
  Check(cleaner->Warmup({}), "second warmup failed");
  cleaner->Process(std::span(fixture).first(3072), true, replay);
  Check(realtime == replay, "independent real stream replay changed");
  report << "real_model_4hop_warmup_zero_state_first_packet_offline_parity_replay=PASS\n";

  // Publish the wider source gains through the actual live processor, and check
  // that mode switches preserve them and rendered PCM agrees with offline DSP.
  for (const RealtimeMix mix : {RealtimeMix{-100,10,-100,100,0}, RealtimeMix{10,-100,10,100,0}}) {
    cleaner->Configure(mix);
    cleaner->SetMode(ProcessingMode::Default);
    Check(cleaner->Controls().effective.music_db == -10, "extended Custom changed Default");
    cleaner->SetMode(ProcessingMode::Off);
    cleaner->SetMode(ProcessingMode::Custom);
    const auto saved = cleaner->Controls();
    Check(saved.effective.voice_db == mix.voice_db && saved.effective.music_db == mix.music_db &&
          saved.effective.other_db == mix.other_db, "extended Custom did not survive mode switches");
    Check(cleaner->Warmup({}), "extended source range warmup failed");
    const auto info = cleaner->Process(std::span(fixture).first(3072), true, realtime);
    Check(info.voice_q12 == (mix.voice_db == -100 ? 0 : 12953) &&
          info.music_q12 == (mix.music_db == -100 ? 0 : 12953) &&
          info.other_q12 == (mix.other_db == -100 ? 0 : 12953), "extended source gain was clipped to the old range");
    offline_cleaner->ResetAll();
    offline_cleaner->SetSourceMixDb(mix.voice_db,mix.music_db,mix.other_db);
    offline_cleaner->ProcessPcm16(std::span(fixture).first(3072), true, offline);
    Check(realtime == offline, "extended live source gains differ from offline rendered PCM");
  }
  cleaner->Configure({-101,11,11,150,-1});
  const auto clamped = cleaner->Controls().custom;
  Check(clamped.voice_db == -100 && clamped.music_db == 10 && clamped.other_db == 10 &&
        clamped.reduction_percent == 100 && clamped.boost_db == 0, "extended live control limits failed");
  report << "extended_source_range=PASS -100_to_10dB all_three_Q12_endpoints mode_preservation live_offline_PCM_error=0 out_of_range_clamped\n";

  // Rapid controls update whole tuples while only the worker touches DSP/level.
  const RealtimeMix mix_a{-20, 0, 6, 30, 2}, mix_b{6, -20, 0, 100, 0};
  cleaner->Configure(mix_a);
  Check(cleaner->Warmup({}), "mix test warmup");
  std::atomic<unsigned> updates{0};
  std::jthread updater([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
      cleaner->Configure((updates.fetch_add(1) % 2) == 0 ? mix_b : mix_a);
      std::this_thread::yield();
    }
  });
  const bool update_thread_ready = WaitUntil([&] { return updates.load() >= 100; });
  std::uint64_t last_revision = 0;
  for (int i = 0; i < 16; ++i) {
    const auto info = cleaner->Process(std::span(fixture).first(i == 0 ? 3072 : 1920), i == 0, realtime);
    const bool a = info.voice_q12 == OfflineCleaner::DbToQ12(-20) &&
                   info.music_q12 == 4096 && info.other_q12 == OfflineCleaner::DbToQ12(6);
    const bool b = info.voice_q12 == OfflineCleaner::DbToQ12(6) &&
                   info.music_q12 == OfflineCleaner::DbToQ12(-20) && info.other_q12 == 4096;
    Check((a || b) && info.mix_revision >= last_revision, "torn mix configuration");
    last_revision = info.mix_revision;
    cleaner->Snapshot();  // concurrent-safe telemetry access
  }
  updater.request_stop(); updater.join();
  Check(update_thread_ready && updates > 100, "configuration stress insufficient");
  report << "rapid_coherent_mix_updates_during_real_model=PASS updates=" << updates << '\n';

  constexpr std::size_t packets = 300;
  cleaner->Configure({});
  auto recorder = std::make_shared<RecordingProcessor>(cleaner, packets);
  auto input = std::make_shared<MemoryInput>(packets); input->paced = true;
  auto output = std::make_shared<MemoryOutput>(packets);
  auto pipeline = std::make_unique<BufferedAudioPipeline>(recorder, input, output);
  const auto start = Clock::now();
  pipeline->Start();
  Check(pipeline->WaitStopped(15s), "real controlled stream timed out");
  const auto snapshot = pipeline->Snapshot();
  AssertDrained(snapshot, packets);
  const auto recovery = cleaner->Snapshot();
  Check(recorder->records.size() == packets && recovery.packets == packets &&
        recovery.ai_runs + recovery.bypasses == packets, "real timing counters");
  std::size_t dry_checked = 0;
  for (std::size_t i = 0; i < recorder->records.size(); ++i) {
    const auto& r = recorder->records[i];
    if (r.work == CleanerWork::Bypass ||
        (r.work == CleanerWork::RecoveryProbe && r.paused)) {
      const auto offset = i * 1920;
      Check(std::equal(output->samples.begin() + offset,
                       output->samples.begin() + offset + 1920,
                       input->samples.begin() + offset),
            "actual-model bypass/probe lost aligned original PCM");
      ++dry_checked;
    }
  }
  report << "actual_model_bypass_muted_probe_original_PCM_alignment="
         << (dry_checked ? "PASS" : "NOT_EXERCISED_NO_FALLBACK")
         << " checked_packets=" << dry_checked << '\n';
  std::ofstream csv(evidence / "real-model-hop-timings.csv");
  csv << "hop,work,engine_ms,cleaner_total_ms,paused,reset,mix_revision\n";
  for (std::size_t i = 0; i < recorder->records.size(); ++i) {
    const auto& r = recorder->records[i];
    csv << i << ',' << static_cast<int>(r.work) << ','
        << std::chrono::duration<double, std::milli>(r.engine_time).count() << ','
        << std::chrono::duration<double, std::milli>(r.total_time).count() << ','
        << r.paused << ',' << r.reset_neural_state << ',' << r.mix_revision << '\n';
  }
  Check(static_cast<bool>(csv), "timing evidence write failed");
  report << "real_model_controlled_stream=PASS packets=" << packets
         << " source_seconds=" << input->samples.size() / 96000.0
         << " wall_seconds=" << std::chrono::duration<double>(Clock::now() - start).count() << '\n';
  report << "queues captured_max=" << snapshot.captured_high_water << " rendered_max=" << snapshot.rendered_high_water
         << " pools_returned=" << snapshot.free_inputs << '/' << snapshot.free_outputs << '\n';
  report << "recovery fallbacks=" << recovery.fallbacks << " recoveries=" << recovery.recoveries
         << " failed_probes=" << recovery.probe_failures << " bypass_packets=" << recovery.bypasses
         << " final_paused=" << recovery.paused << '\n';
  PrintTiming(report, "real_ai_decode_DSP_model", snapshot.ai_engine);
  PrintTiming(report, "real_ai_full_processor", snapshot.ai_packet);
  PrintTiming(report, "real_bypass_full_processor", snapshot.bypass_packet);
  report << "Timings exclude capture/playback waits and warmup. This is a paced synthetic stream, no Windows audio API.\n";
}

int Run(const std::filesystem::path& model, const std::filesystem::path& rise,
         const std::filesystem::path& hop, const std::filesystem::path& evidence) {
  std::filesystem::create_directories(evidence);
  std::ostringstream report;
  report << "CleanWave Step 5 real-time worker verification\n"
         << "format=48000Hz stereo PCM16 first=1536 hop=960 queue_capacity=3 pool_capacity=5\n";
  try {
    TestRecovery(report);
    TestWorkers(report);
    TestDirectPlayback(report);
    TestFailuresAndCancellation(report);
    TestRealModel(model, rise, hop, evidence, report);
    report << "RESULT=PASS\n";
  } catch (const std::exception& e) {
    report << "FAILURE=" << e.what() << "\nRESULT=FAIL\n";
    std::ofstream(evidence / "realtime-verification.txt") << report.str();
    std::cerr << report.str();
    return 1;
  }
  std::ofstream(evidence / "realtime-verification.txt") << report.str();
  std::cout << report.str();
  return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 5) {
    std::cerr << "Usage: cleanwave_realtime_verify <model.tflite> <rise.bin> <hop.bin> <evidence-dir>\n";
    return 2;
  }
  try { return Run(argv[1], argv[2], argv[3], argv[4]); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
