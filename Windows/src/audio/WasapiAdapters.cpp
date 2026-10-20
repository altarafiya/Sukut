#include "audio/WinAudio.h"
#include "audio/WasapiAdapters.h"
#include "audio/FloatPcm.h"

#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>
#include <cassert>
#include <chrono>
#include <cmath>

namespace cleanwave {
using namespace audio_detail;
namespace {
class AudioTask {
 public:
  explicit AudioTask(bool enabled = true) {
    if (!enabled) return;
    DWORD index = 0;
    handle = AvSetMmThreadCharacteristicsW(L"Audio", &index);
    if (!handle) error = GetLastError();
    else AvSetMmThreadPriority(handle, AVRT_PRIORITY_HIGH);
    THREAD_POWER_THROTTLING_STATE previous{}; previous.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    if (GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &previous, sizeof(previous))) {
      previous_ = previous; restore_qos = true;
      THREAD_POWER_THROTTLING_STATE requested{}; requested.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
      requested.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
      high_qos = SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &requested, sizeof(requested)) != FALSE;
    }
  }
  ~AudioTask() {
    if (high_qos && restore_qos) SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &previous_, sizeof(previous_));
    if (handle) AvRevertMmThreadCharacteristics(handle);
  }
  HANDLE handle = nullptr;
  DWORD error = 0;
  bool high_qos = false, restore_qos = false;
  THREAD_POWER_THROTTLING_STATE previous_{};
};
class RefillTimer {
 public:
  RefillTimer() {
    handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    high_resolution = handle != nullptr;
    if (!handle && GetLastError() == ERROR_INVALID_PARAMETER)
      handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!handle) throw std::runtime_error("CreateWaitableTimerEx failed");
  }
  ~RefillTimer() { CloseHandle(handle); }
  void Arm() {
    LARGE_INTEGER due{}; due.QuadPart = -20000; // relative 2 ms; no global timer-resolution change
    if (!SetWaitableTimer(handle, &due, 0, nullptr, nullptr, FALSE)) throw std::runtime_error("SetWaitableTimer failed");
  }
  HANDLE handle = nullptr;
  bool high_resolution = false;
};
class Session {
 public:
  Session(const std::wstring& id, EDataFlow flow, std::uint32_t rate, std::uint32_t buffer_ms, bool schedule_audio = true)
      : task(schedule_audio), owner(GetCurrentThreadId()), cancel(true) {
    if (rate < 8000 || rate > 192000) throw std::invalid_argument("unsupported requested sample rate");
    if (buffer_ms < 20 || buffer_ms > 500) throw std::invalid_argument("invalid endpoint buffer duration");
    auto device = Device(id);
    CheckFlow(device.Get(), flow);
    CheckHr(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "Activate IAudioClient");
    if (flow == eRender) {
      // Remain in the shared Windows effects path. Never request RAW, exclusive
      // or offloaded playback: those can bypass device/vendor processing.
      ComPtr<IAudioClient2> media_client;
      if (SUCCEEDED(client.As(&media_client))) {
        AudioClientProperties properties{};
        properties.cbSize = sizeof(properties);
        properties.bIsOffload = FALSE;
        properties.eCategory = AudioCategory_Media;
        properties.Options = AUDCLNT_STREAMOPTIONS_NONE;
        // Unsupported categories retain the normal shared-mode default.
        initial.media_category_requested = SUCCEEDED(media_client->SetClientProperties(&properties));
      }
    }
    WAVEFORMATEX* mix = nullptr;
    CheckHr(client->GetMixFormat(&mix), "GetMixFormat");
    initial.client_rate = rate;
    initial.mmcss_registered = task.handle != nullptr;
    initial.mmcss_error = task.error;
    initial.high_qos = task.high_qos;
    initial.processing_thread_owns_schedule = !schedule_audio;
    initial.high_resolution_timer = timer.high_resolution;
    initial.mix_rate = mix->nSamplesPerSec;
    initial.mix_channels = mix->nChannels;
    CoTaskMemFree(mix);
    WAVEFORMATEXTENSIBLE requested{};
    requested.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    requested.Format.nChannels = 2;
    requested.Format.nSamplesPerSec = rate;
    requested.Format.wBitsPerSample = 32;
    requested.Format.nBlockAlign = 8;
    requested.Format.nAvgBytesPerSec = rate * 8;
    requested.Format.cbSize = 22;
    requested.Samples.wValidBitsPerSample = 32;
    requested.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    requested.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    GUID session;
    CheckHr(CoCreateGuid(&session), "CoCreateGuid");
    CheckHr(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST,
        static_cast<REFERENCE_TIME>(buffer_ms) * 10000, 0, &requested.Format, &session), "Initialize buffered shared float stereo conversion");
    // Shared event mode fixes buffer capacity at the engine minimum (~22 ms).
    // Timer-driven shared mode permits headroom for 20-ms model hops. Capture
    // capacity is spare storage, not a wait-to-fill target; drain promptly.
    UINT32 size = 0;
    CheckHr(client->GetBufferSize(&size), "GetBufferSize");
    if (size == 0) throw std::runtime_error("empty WASAPI endpoint buffer");
    initial.buffer_frames = size;
    REFERENCE_TIME latency = 0;
    CheckHr(client->GetStreamLatency(&latency), "GetStreamLatency");
    initial.reported_stream_latency_ms = latency / 10000.0;
    initial.open = true;
  }
  ~Session() { if (started) client->Stop(); }
  void AssertOwner() const {
    if (owner != GetCurrentThreadId()) throw std::logic_error("WASAPI call on wrong owner thread");
  }
  bool Wait(std::stop_token stop, WasapiSnapshot& snapshot) {
    AssertOwner();
    if (stop.stop_requested()) return false;
    std::stop_callback callback(stop, [this] { cancel.Signal(); });
    timer.Arm();
    HANDLE handles[] = {cancel.Get(), timer.handle};
    const DWORD result = WaitForMultipleObjects(2, handles, FALSE, 100);
    if (result == WAIT_OBJECT_0 || stop.stop_requested()) return false;
    if (result == WAIT_OBJECT_0 + 1) { ++snapshot.event_wakes; return true; }
    if (result == WAIT_TIMEOUT) { ++snapshot.wait_timeouts; return false; }
    throw std::runtime_error("audio event wait failed");
  }
  void Start() { CheckHr(client->Start(), "Start audio client"); started = true; }
  // Declare apartment first: it is destroyed last, after COM service/client release.
  Apartment apartment;
  AudioTask task;
  DWORD owner;
  RefillTimer timer;
  Event cancel;
  ComPtr<IAudioClient> client;
  bool started = false;
  WasapiSnapshot initial;
};
}

class WasapiCapture::Impl {
 public:
  Impl(const std::wstring& id, std::uint32_t rate, std::uint32_t buffer_ms) : session(id, eCapture, rate, buffer_ms) {
    CheckHr(session.client->GetService(IID_PPV_ARGS(&capture)), "GetService capture");
    pending.resize(session.initial.buffer_frames * 2);
    session.Start();
  }
  Session session;
  ComPtr<IAudioCaptureClient> capture;
  std::vector<std::int16_t> pending;
  std::size_t cursor = 0, count = 0;
  bool first_packet = true;
  bool pending_discontinuity = false;
  std::uint64_t expected_position = 0, last_qpc = 0;
  bool position_valid = false;
  std::chrono::steady_clock::time_point last_read{};
};
WasapiCapture::WasapiCapture(std::wstring id, std::uint32_t rate, bool resume, std::uint32_t buffer_ms)
    : endpoint_id_(std::move(id)), rate_(rate), resume_on_discontinuity_(resume), buffer_ms_(buffer_ms) {}
WasapiCapture::~WasapiCapture() { assert(!impl_ && "Close WASAPI on its opening thread before destruction"); }
void WasapiCapture::Open(std::stop_token stop) {
  if (impl_) throw std::logic_error("capture already open");
  if (stop.stop_requested()) return;
  auto impl = std::make_unique<Impl>(endpoint_id_, rate_, buffer_ms_);
  { std::lock_guard lock(mutex_); snapshot_ = impl->session.initial; }
  impl_ = std::move(impl);
}
void WasapiCapture::Close() noexcept {
  if (impl_) {
    assert(impl_->session.owner == GetCurrentThreadId());
    impl_.reset();
  }
  std::lock_guard lock(mutex_); snapshot_.open = false;
}
WasapiSnapshot WasapiCapture::Snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }

ReadResult WasapiCapture::Read(std::span<std::int16_t> destination, std::stop_token stop) {
  if (stop.stop_requested()) return {0, ReadStatus::WouldBlock};
  if (!impl_ || destination.empty() || destination.size() % 2 != 0) throw std::logic_error("invalid capture read");
  auto& state = *impl_;
  state.session.AssertOwner();
  auto stats = Snapshot();
  if (state.cursor == state.count) {
    UINT32 available = 0;
    CheckHr(state.capture->GetNextPacketSize(&available), "GetNextPacketSize");
    if (available == 0) {
      state.session.Wait(stop, stats);
      { std::lock_guard lock(mutex_); snapshot_ = stats; }
      if (stop.stop_requested()) return {0, ReadStatus::WouldBlock};
      CheckHr(state.capture->GetNextPacketSize(&available), "GetNextPacketSize");
      if (available == 0) return {0, ReadStatus::WouldBlock};
    }
    BYTE* bytes = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    UINT64 position = 0, qpc = 0;
    CheckHr(state.capture->GetBuffer(&bytes, &frames, &flags, &position, &qpc), "capture GetBuffer");
    struct Release {
      IAudioCaptureClient* capture; UINT32 frames;
      ~Release() { if (frames) capture->ReleaseBuffer(frames); }
    } release{state.capture.Get(), frames};
    if (frames == 0) {
      // A driver can report an empty buffer after the size query. Honor the
      // adapter-owned-wait contract even on that race; never spin the worker.
      state.session.Wait(stop, stats);
      { std::lock_guard lock(mutex_); snapshot_ = stats; }
      return {0, ReadStatus::WouldBlock};
    }
    if (frames * 2 > state.pending.size()) throw std::runtime_error("capture packet exceeds fixed staging capacity");
    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
      if (state.first_packet) ++stats.startup_discontinuities;
      else {
        ++stats.discontinuities;
        if (!resume_on_discontinuity_) {
          std::lock_guard lock(mutex_); snapshot_ = stats;
          throw std::runtime_error("capture discontinuity: restart stream before using neural state");
        }
        state.pending_discontinuity = true;
      }
    }
    if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++stats.timestamp_errors;
    const auto read_time = std::chrono::steady_clock::now();
    if(resume_on_discontinuity_ && state.last_read.time_since_epoch().count()
        && read_time-state.last_read>std::chrono::milliseconds(200))
      state.pending_discontinuity=true; // resume a fresh model segment after idle
    if (state.last_read.time_since_epoch().count()) stats.max_capture_read_interval_ms =
        std::max(stats.max_capture_read_interval_ms,
            std::chrono::duration<double, std::milli>(read_time - state.last_read).count());
    state.last_read = read_time;
    if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
      if (state.position_valid && position > state.expected_position) {
        ++stats.capture_position_gaps;
        stats.capture_missing_frames += position - state.expected_position;
      }
      if (state.last_qpc && qpc >= state.last_qpc)
        stats.max_capture_packet_interval_ms = std::max(stats.max_capture_packet_interval_ms,
            (qpc - state.last_qpc) / 10000.0);
      state.expected_position = position + frames; state.last_qpc = qpc; state.position_valid = true;
    } else state.position_valid = false;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::fill_n(state.pending.begin(), frames * 2, std::int16_t{0});
    else {
      if (!bytes && frames) throw std::runtime_error("null non-silent capture packet");
      FloatToPcm16(std::span(reinterpret_cast<const float*>(bytes), frames * 2),
                   std::span(state.pending).first(frames * 2));
    }
    const HRESULT released = state.capture->ReleaseBuffer(frames);
    release.frames = 0;
    CheckHr(released, "capture ReleaseBuffer");
    state.cursor = 0; state.count = frames * 2; state.first_packet = false;
    ++stats.packets;
  }
  const std::size_t samples = std::min(destination.size(), state.count - state.cursor);
  std::copy_n(state.pending.begin() + state.cursor, samples, destination.begin());
  state.cursor += samples;
  const bool discontinuity = samples && state.pending_discontinuity;
  if (samples) state.pending_discontinuity = false;
  stats.frames += samples / 2;
  { std::lock_guard lock(mutex_); snapshot_ = stats; }
  return {samples / 2, samples ? ReadStatus::Data : ReadStatus::WouldBlock, discontinuity};
}

class WasapiRender::Impl {
 public:
  Impl(const std::wstring& id, std::uint32_t rate, std::uint32_t buffer_ms, bool schedule_audio) : session(id, eRender, rate, buffer_ms, schedule_audio) {
    CheckHr(session.client->GetService(IID_PPV_ARGS(&render)), "GetService render");
    CheckHr(session.client->GetService(IID_PPV_ARGS(&clock)), "GetService render clock");
    CheckHr(clock->GetFrequency(&frequency), "render clock GetFrequency");
    if (!frequency) throw std::runtime_error("invalid render clock frequency");
  }
  Session session;
  ComPtr<IAudioRenderClient> render;
  ComPtr<IAudioClock> clock;
  UINT64 frequency = 0;
  std::chrono::steady_clock::time_point last_write{};
  std::uint64_t next_frame = 0;
  float amplification_gain = 1.0f;
};
WasapiRender::WasapiRender(std::wstring id, std::uint32_t rate, std::uint32_t buffer_ms, std::uint32_t prefill_ms,
                         std::shared_ptr<std::atomic<int>> amplification_db, bool schedule_audio)
    : endpoint_id_(std::move(id)), rate_(rate), buffer_ms_(buffer_ms), prefill_ms_(prefill_ms), amplification_db_(std::move(amplification_db)), schedule_audio_(schedule_audio) {
  if (prefill_ms == 0 || prefill_ms > buffer_ms) throw std::invalid_argument("invalid render prefill duration");
}
WasapiRender::~WasapiRender() { assert(!impl_ && "Close WASAPI on its opening thread before destruction"); }
void WasapiRender::Open(std::stop_token stop) {
  if (impl_) throw std::logic_error("render already open");
  if (stop.stop_requested()) return;
  auto impl = std::make_unique<Impl>(endpoint_id_, rate_, buffer_ms_, schedule_audio_);
  { std::lock_guard lock(mutex_); snapshot_ = impl->session.initial; }
  impl_ = std::move(impl);
}
void WasapiRender::Close() noexcept {
  if (impl_) { assert(impl_->session.owner == GetCurrentThreadId()); impl_.reset(); }
  std::lock_guard lock(mutex_); snapshot_.open = false;
}
WasapiSnapshot WasapiRender::Snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }
std::size_t WasapiRender::Write(std::span<const std::int16_t> source,
                                std::uint64_t first_frame, std::stop_token stop) {
  if (stop.stop_requested()) return 0;
  if (!impl_ || source.empty() || source.size() % 2 != 0) throw std::logic_error("invalid render write");
  auto& state = *impl_; state.session.AssertOwner();
  if (first_frame != state.next_frame) throw std::runtime_error("render timeline mismatch");
  auto stats = Snapshot();
  UINT32 padding = 0;
  CheckHr(state.session.client->GetCurrentPadding(&padding), "render GetCurrentPadding");
  if (padding > stats.buffer_frames) throw std::runtime_error("invalid render padding");
  if (padding == stats.buffer_frames) {
    state.session.Wait(stop, stats);
    { std::lock_guard lock(mutex_); snapshot_ = stats; }
    if (stop.stop_requested()) return 0;
    CheckHr(state.session.client->GetCurrentPadding(&padding), "render GetCurrentPadding");
    if (padding == stats.buffer_frames) return 0;
    if (padding > stats.buffer_frames) throw std::runtime_error("invalid render padding");
  }
  if (padding == 0 && state.session.started) ++stats.empty_render_observations;
  if (state.session.started) {
    UINT64 position = 0;
    const auto hr = state.clock->GetPosition(&position, nullptr);
    if (hr == S_OK) {
      const auto consumed = static_cast<std::uint64_t>(static_cast<long double>(position) * rate_ / state.frequency);
      if (consumed + padding > stats.frames)
        stats.render_clock_deficit_frames = std::max(stats.render_clock_deficit_frames, consumed + padding - stats.frames);
    } else ++stats.clock_read_errors;
  }
  const auto frames = static_cast<UINT32>(std::min<std::size_t>(source.size() / 2, stats.buffer_frames - padding));
  BYTE* bytes = nullptr;
  CheckHr(state.render->GetBuffer(frames, &bytes), "render GetBuffer");
  auto pcm = source.first(frames * 2);
  auto converted = std::span(reinterpret_cast<float*>(bytes), frames * 2);
  const int boost_db = amplification_db_ ? std::clamp(amplification_db_->load(std::memory_order_relaxed),0,12) : 0;
  if (boost_db == 0 && state.amplification_gain <= 1.00001f) {
    state.amplification_gain = 1.0f;
    Pcm16ToFloat(pcm,converted); // Exact existing conversion at the default.
  } else {
    const float requested = std::pow(10.0f,boost_db/20.0f);
    int peak = 0;
    for (const auto sample : pcm) peak = std::max(peak,std::abs(static_cast<int>(sample)));
    const float safe = peak ? std::max(1.0f,32767.0f/peak) : requested;
    const float target = std::min(requested,safe);
    state.amplification_gain = std::min(state.amplification_gain,safe);
    // Gain response only: no lookahead, additional frames or queue. Stereo
    // channels share one gain; limit to this already-available chunk's headroom.
    const float smoothing = 1.0f/std::max(1u,rate_/200);
    for (UINT32 frame = 0; frame < frames; ++frame) {
      state.amplification_gain += (target-state.amplification_gain)*smoothing;
      converted[frame*2] = pcm[frame*2]*(state.amplification_gain/32768.0f);
      converted[frame*2+1] = pcm[frame*2+1]*(state.amplification_gain/32768.0f);
    }
  }
  CheckHr(state.render->ReleaseBuffer(frames, 0), "render ReleaseBuffer");
  if (!state.session.started) {
    stats.startup_prefill_frames += frames;
    // Reserve actual audio for the selected profile before starting playback.
    // Queue actual audio only: no inserted silence or duplicated samples.
    const auto target = std::min(stats.buffer_frames, rate_ * prefill_ms_ / 1000);
    if (stats.startup_prefill_frames >= target) state.session.Start();
  }
  const auto now = std::chrono::steady_clock::now();
  if (state.last_write.time_since_epoch().count()) stats.max_render_write_interval_ms =
      std::max(stats.max_render_write_interval_ms, std::chrono::duration<double, std::milli>(now - state.last_write).count());
  state.last_write = now;
  state.next_frame += frames; stats.frames += frames; ++stats.packets;
  { std::lock_guard lock(mutex_); snapshot_ = stats; }
  return frames;
}
void WasapiRender::Finish(std::stop_token stop) {
  if (!impl_ || stop.stop_requested()) return;
  auto& state = *impl_; state.session.AssertOwner();
  // Normal EOS shorter than the startup reserve must still play all samples.
  if (!state.session.started) {
    if (state.next_frame == 0) return;
    state.session.Start();
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  auto stats = Snapshot();
  while (!stop.stop_requested()) {
    UINT32 padding = 0;
    CheckHr(state.session.client->GetCurrentPadding(&padding), "drain GetCurrentPadding");
    if (padding == 0) break;
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("render drain timeout");
    state.session.Wait(stop, stats);
  }
  std::lock_guard lock(mutex_); snapshot_ = stats;
}
}  // namespace cleanwave
