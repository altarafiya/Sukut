#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "audio/AudioEndpoints.h"
#include "audio/FloatPcm.h"
#include "audio/WasapiAdapters.h"

using namespace cleanwave;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <typename Function> void MustFail(Function function, const char* message) {
  bool failed = false;
  try { function(); } catch (const std::exception&) { failed = true; }
  Check(failed, message);
}
template <typename Adapter> struct CloseOnExit { Adapter& adapter; ~CloseOnExit() { adapter.Close(); } };

void TestConversion(std::ostringstream& report) {
  std::vector<std::int16_t> pcm(65536), back(65536);
  std::vector<float> floats(65536);
  for (int i = -32768; i <= 32767; ++i) pcm[i + 32768] = static_cast<std::int16_t>(i);
  Pcm16ToFloat(pcm, floats); FloatToPcm16(floats, back);
  Check(pcm == back, "PCM16/float roundtrip error");
  const std::array<float, 6> bounds{-2, -1, -.5F, 0, .5F, 2};
  std::array<std::int16_t, 6> result;
  FloatToPcm16(bounds, result);
  Check(result == std::array<std::int16_t, 6>{-32768,-32768,-16384,0,16384,32767}, "capture clipping wrong");
  for (float value : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
    std::array<float, 1> bad{value}; std::array<std::int16_t, 1> out;
    MustFail([&] { FloatToPcm16(bad, out); }, "nonfinite capture accepted");
  }
  report << "all_65536_PCM16_float_roundtrips_clip_nonfinite=PASS\n";
}

void TestRoutes(const AudioRoute& route, const std::vector<EndpointInfo>& endpoints,
                 std::ostringstream& report) {
  ValidateAudioRoute(route, endpoints);
  AudioRoute bad = route; bad.physical_render_id = route.source_render_id;
  MustFail([&] { ValidateAudioRoute(bad, endpoints); }, "virtual feedback render accepted");
  bad = route; bad.capture_id = route.source_render_id;
  MustFail([&] { ValidateAudioRoute(bad, endpoints); }, "render endpoint accepted as capture");
  bad = route; bad.source_render_id = route.physical_render_id;
  MustFail([&] { ValidateAudioRoute(bad, endpoints); }, "physical source loopback accepted");
  bad = route; bad.capture_id = L"missing endpoint";
  MustFail([&] { ValidateAudioRoute(bad, endpoints); }, "missing endpoint accepted");
  auto inactive = endpoints;
  for (auto& endpoint : inactive) if (endpoint.id == route.capture_id) endpoint.state = 4;
  MustFail([&] { ValidateAudioRoute(route, inactive); }, "inactive capture accepted");
  auto ambiguous = endpoints;
  for (auto& endpoint : ambiguous) if (endpoint.id == route.capture_id) endpoint.adapter_instance_id.clear();
  MustFail([&] { ValidateAudioRoute(route, ambiguous); }, "unverified cable pair accepted");
  auto software_output = endpoints;
  for (auto& endpoint : software_output) if (endpoint.id == route.physical_render_id) {
    endpoint.name = L"Other virtual device"; endpoint.adapter_instance_id = L"ROOT\\MEDIA\\9999";
  }
  MustFail([&] { ValidateAudioRoute(route, software_output); }, "unverified software output accepted as physical");
  report << "real_route_and_feedback_direction_inactive_missing_pair_guards=PASS\n";
}

class SilentInput final : public StreamInput {
 public:
  ReadResult Read(std::span<std::int16_t> dest, std::stop_token stop) override {
    if (stop.stop_requested()) return {0, ReadStatus::WouldBlock};
    if (remaining == 0) return {0, ReadStatus::End};
    const auto frames = std::min(dest.size() / 2, remaining);
    std::fill_n(dest.begin(), frames * 2, std::int16_t{0});
    remaining -= frames;
    return {frames, ReadStatus::Data};
  }
  std::size_t remaining = 12 * 960 + 576;
};
class SilentPassThrough final : public PacketProcessor {
 public:
  bool Warmup(std::stop_token stop) override { return !stop.stop_requested(); }
  ProcessInfo Process(std::span<const std::int16_t> input, bool, AudioLevel::PcmHop& output) override {
    Check(std::all_of(input.begin(), input.end(), [](auto x) { return x == 0; }), "physical silent fixture is nonzero");
    output.fill(0);
    ProcessInfo info; info.work = CleanerWork::Bypass; return info;
  }
};

void TestPhysicalSilence(const std::wstring& id, std::ostringstream& report) {
  auto sink = std::make_shared<WasapiRender>(id);
  auto pipeline = std::make_unique<BufferedAudioPipeline>(std::make_shared<SilentPassThrough>(),
                                                        std::make_shared<SilentInput>(), sink);
  pipeline->Start();
  Check(pipeline->WaitStopped(3s), "physical silent render cleanup timeout");
  const auto stats = pipeline->Snapshot();
  const auto audio = sink->Snapshot();
  Check(stats.failure.empty() && stats.played == 12 && stats.processed == 12 &&
        !audio.open && audio.frames == 12 * 960 && stats.free_inputs == 5 && stats.free_outputs == 5,
        "physical silent pipeline failed");
  Check(stats.capture_first_read_order > stats.playback_ready_order,
        "physical adapter was not ready before capture");
  report << "physical_silent_render_pipeline=PASS frames=" << audio.frames
         << " buffer_frames=" << audio.buffer_frames << " reported_stream_latency_ms="
         << audio.reported_stream_latency_ms << " events=" << audio.event_wakes << '\n';
  // A finite stream shorter than the three-hop reserve still starts at EOS.
  WasapiRender short_stream(id); short_stream.Open({});
  CloseOnExit<WasapiRender> close{short_stream};
  std::array<std::int16_t, 1920> silence{};
  const auto begin = Clock::now();
  Check(short_stream.Write(silence, 0, {}) == 960, "short render did not accept its only hop");
  short_stream.Finish({});
  Check(short_stream.Snapshot().frames == 960 && Clock::now()-begin < 1s,
        "short EOS did not drain below startup threshold");
  report << "short_normal_EOS_below_startup_reserve=PASS frames=960\n";
}

double ToneAmplitude(const std::vector<std::int16_t>& pcm, std::size_t first,
                      std::size_t frames, unsigned channel, double frequency, unsigned rate) {
  double real = 0, imaginary = 0;
  constexpr double pi = 3.14159265358979323846;
  for (std::size_t i = 0; i < frames; ++i) {
    const double angle = 2 * pi * frequency * i / rate;
    const double value = pcm[(first + i) * 2 + channel];
    real += value * std::cos(angle); imaginary += value * std::sin(angle);
  }
  return 2 * std::hypot(real, imaginary) / frames;
}

void TestVirtualTransport(const AudioRoute& route, unsigned render_rate, unsigned capture_rate,
                          std::ostringstream& report) {
  std::vector<std::int16_t> received;
  received.reserve(capture_rate * 2 * 4);
  WasapiCapture capture(route.capture_id, capture_rate);
  WasapiRender source(route.source_render_id, render_rate);
  std::mutex mutex;
  std::condition_variable ready;
  bool opened = false;
  std::exception_ptr capture_error, source_error;
  std::jthread recorder([&](std::stop_token stop) {
    CloseOnExit close{capture};
    try {
      capture.Open(stop);
      { std::lock_guard lock(mutex); opened = true; }
      ready.notify_all();
      std::array<std::int16_t, 3072> packet;
      const auto deadline = Clock::now() + 3s;
      while (!stop.stop_requested() && Clock::now() < deadline) {
        const auto result = capture.Read(packet, stop);
        if (result.frames) {
          Check(received.size() + result.frames * 2 <= received.capacity(), "capture fixture bound exceeded");
          received.insert(received.end(), packet.begin(), packet.begin() + result.frames * 2);
        }
      }
    } catch (...) {
      { std::lock_guard lock(mutex); capture_error = std::current_exception(); opened = true; }
      ready.notify_all();
    }
  });
  bool startup_ready = false, startup_failed = false;
  {
    std::unique_lock lock(mutex);
    startup_ready = ready.wait_for(lock, 2s, [&] { return opened; });
    startup_failed = static_cast<bool>(capture_error);
  }
  if (!startup_ready) {
    recorder.request_stop(); recorder.join(); throw std::runtime_error("capture startup timeout");
  }
  if (!startup_failed) {
    std::thread writer([&] {
      CloseOnExit close{source};
      try {
        source.Open({});
        // Nonzero stereo fixture stays exclusively on the virtual cable.
        const std::size_t frames = render_rate * 8 / 10;
        std::vector<std::int16_t> pcm(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
          const double t = static_cast<double>(i) / render_rate;
          if (t >= .1 && t < .65) {
            pcm[i * 2] = static_cast<std::int16_t>(9000 * std::sin(2 * 3.14159265358979323846 * 337 * t));
            pcm[i * 2 + 1] = static_cast<std::int16_t>(7000 * std::cos(2 * 3.14159265358979323846 * 719 * t));
          }
        }
        std::size_t cursor = 0;
        const auto deadline = Clock::now() + 2s;
        while (cursor < frames) {
          Check(Clock::now() < deadline, "virtual render fixture timeout");
          cursor += source.Write(std::span(pcm).subspan(cursor * 2), cursor, {});
        }
        source.Finish({});
      } catch (...) { source_error = std::current_exception(); }
    });
    writer.join();
    std::this_thread::sleep_for(100ms);
  }
  recorder.request_stop(); recorder.join();
  if (capture_error) std::rethrow_exception(capture_error);
  if (source_error) std::rethrow_exception(source_error);
  const std::size_t window = capture_rate / 5;
  Check(received.size() / 2 >= window * 2, "too little captured cable audio");
  double best = 0, left_wanted = 0, right_wanted = 0, left_wrong = 0, right_wrong = 0;
  for (std::size_t first = 0; first + window <= received.size() / 2; first += capture_rate / 20) {
    const double left = ToneAmplitude(received, first, window, 0, 337, capture_rate);
    const double right = ToneAmplitude(received, first, window, 1, 719, capture_rate);
    if (left + right > best) {
      best = left + right; left_wanted = left; right_wanted = right;
      left_wrong = ToneAmplitude(received, first, window, 0, 719, capture_rate);
      right_wrong = ToneAmplitude(received, first, window, 1, 337, capture_rate);
    }
  }
  Check(left_wanted > 5 && right_wanted > 5 && left_wrong < left_wanted * .1 &&
        right_wrong < right_wanted * .1, "virtual stereo transport/resampling lost channel tones");
  Check(!capture.Snapshot().open && !source.Snapshot().open, "virtual endpoints not closed");
  report << "virtual_transport=PASS render_rate=" << render_rate << " capture_rate=" << capture_rate
         << " engine_mix_rate=" << source.Snapshot().mix_rate << '/' << capture.Snapshot().mix_rate
         << " received_frames=" << received.size() / 2 << " tone_PCM_amplitudes=" << left_wanted << '/' << right_wanted
         << " wrong_channel_amplitudes=" << left_wrong << '/' << right_wrong
         << " buffer_frames=" << source.Snapshot().buffer_frames << '/' << capture.Snapshot().buffer_frames
         << " reported_stream_latency_ms=" << source.Snapshot().reported_stream_latency_ms << '/'
         << capture.Snapshot().reported_stream_latency_ms << '\n';
}

void TestNativeCancellation(const std::wstring& id, std::ostringstream& report) {
  double maximum = 0;
  for (int i = 0; i < 6; ++i) {
    auto capture = std::make_shared<WasapiCapture>(id);
    std::atomic<bool> opened{false};
    std::exception_ptr error;
    std::jthread worker([&](std::stop_token stop) {
      CloseOnExit close{*capture};
      try {
        capture->Open(stop); opened = true;
        std::array<std::int16_t, 1920> packet;
        while (!stop.stop_requested()) capture->Read(packet, stop);
      } catch (...) { error = std::current_exception(); opened = true; }
    });
    const auto deadline = Clock::now() + 2s;
    while (!opened && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    const auto cancel_start = Clock::now(); worker.request_stop(); worker.join();
    if (error) std::rethrow_exception(error);
    Check(opened && !capture->Snapshot().open, "native capture cleanup failed");
    maximum = std::max(maximum, std::chrono::duration<double, std::milli>(Clock::now() - cancel_start).count());
  }
  report << "native_capture_cancel_restart_6_cycles=PASS max_shutdown_ms=" << maximum << '\n';
}

void TestErrors(const AudioRoute& route, std::ostringstream& report) {
  WasapiCapture wrong(route.physical_render_id);
  MustFail([&] { wrong.Open({}); }, "render endpoint capture should fail"); wrong.Close();
  WasapiRender absent(L"missing endpoint");
  MustFail([&] { absent.Open({}); }, "missing output should fail"); absent.Close();
  auto incompatible = std::make_shared<WasapiRender>(route.physical_render_id, 44100);
  auto pipeline = std::make_unique<BufferedAudioPipeline>(std::make_shared<SilentPassThrough>(),
                                                        std::make_shared<SilentInput>(), incompatible);
  MustFail([&] { pipeline->Start(); }, "worker should reject non-48k adapter");
  report << "native_wrong_direction_missing_endpoint_and_worker_rate_rejection=PASS\n";
}

int Run(const AudioRoute& route, const std::filesystem::path& evidence) {
  std::filesystem::create_directories(evidence);
  std::ostringstream report;
  report << "CleanWave Step 6 WASAPI backend verification\n"
         << "nonzero_fixture_destination=virtual_CABLE_only physical_fixture=silence no_model_loaded\n";
  try {
    const auto before = EnumerateAudioEndpoints();
    TestConversion(report); TestRoutes(route, before, report); TestErrors(route, report);
    TestVirtualTransport(route, 48000, 48000, report);
    TestVirtualTransport(route, 44100, 48000, report);
    TestVirtualTransport(route, 48000, 44100, report);
    TestPhysicalSilence(route.physical_render_id, report);
    TestNativeCancellation(route.capture_id, report);
    const auto after = EnumerateAudioEndpoints();
    for (const auto& old : before) {
      const auto it = std::find_if(after.begin(), after.end(), [&](const auto& current) { return current.id == old.id; });
      Check(it != after.end() && it->state == old.state && it->default_console == old.default_console &&
            it->default_multimedia == old.default_multimedia && it->default_communications == old.default_communications &&
            it->mix_rate == old.mix_rate && it->channels == old.channels && it->bits == old.bits &&
            it->muted == old.muted && std::abs(it->volume - old.volume) < .00001F,
            "endpoint settings changed during native verification");
    }
    report << "endpoint_defaults_mix_format_mute_volume_unchanged=PASS\nRESULT=PASS\n";
  } catch (const std::exception& error) {
    report << "FAILURE=" << error.what() << "\nRESULT=FAIL\n";
    std::ofstream(evidence / "audio-verification.txt") << report.str();
    std::cerr << report.str(); return 1;
  }
  std::ofstream(evidence / "audio-verification.txt") << report.str();
  std::cout << report.str(); return 0;
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 5) { std::cerr << "Usage: cleanwave_audio_verify <virtual-render-id> <capture-id> <physical-render-id> <evidence-dir>\n"; return 2; }
  try { return Run({argv[1], argv[2], argv[3]}, argv[4]); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
