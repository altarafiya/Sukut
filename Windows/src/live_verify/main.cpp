#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
#include "dsp/OfflineCleaner.h"
#include "live/LiveSession.h"
#include "offline/WavIO.h"

using namespace cleanwave;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
namespace {
struct Assets { std::filesystem::path model, rise, hop, source_exe, evidence; bool save_audio = true; };
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void MustFail(F&& action, const char* message) {
  try { action(); } catch (const std::exception&) { return; }
  throw std::runtime_error(message);
}
bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 3s) {
  const auto end = Clock::now() + timeout;
  do { if (predicate()) return true; std::this_thread::sleep_for(2ms); } while (Clock::now() < end);
  return predicate();
}
std::wstring Quote(const std::wstring& value) {
  if (value.find(L'"') != std::wstring::npos) throw std::invalid_argument("unexpected quote in process argument");
  return L"\"" + value + L"\"";
}
class SourceProcess {
 public:
  SourceProcess(const Assets& assets, const AudioRoute& route, unsigned rate, unsigned seconds,
                const std::wstring& label) {
    std::wstring command = Quote(assets.source_exe.wstring()) + L" " + Quote(route.source_render_id)
        + L" " + std::to_wstring(rate) + L" " + std::to_wstring(seconds) + L" "
        + Quote((assets.evidence / (label + L"-source.txt")).wstring());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    if (!CreateProcessW(assets.source_exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_))
      throw std::runtime_error("cannot start independent live source");
    CloseHandle(process_.hThread);
  }
  ~SourceProcess() {
    if (WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(process_.hProcess, 1); WaitForSingleObject(process_.hProcess, 1000);
    }
    CloseHandle(process_.hProcess);
  }
  void Wait(unsigned seconds) {
    Check(WaitForSingleObject(process_.hProcess, seconds * 1000) == WAIT_OBJECT_0, "source process timeout");
    DWORD code; Check(GetExitCodeProcess(process_.hProcess, &code) && code == 0, "source process failed");
  }
 private: PROCESS_INFORMATION process_{};
};

std::vector<std::int16_t> Fixture(std::size_t frames) {
  std::vector<std::int16_t> result(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 48000;
    result[i * 2] = static_cast<std::int16_t>(3000 * std::sin(2 * 3.141592653589793 * 337 * t));
    result[i * 2 + 1] = static_cast<std::int16_t>(2300 * std::cos(2 * 3.141592653589793 * 719 * t));
  }
  return result;
}

void TestModes(const Assets& a, std::ostringstream& report) {
  RealtimeCleaner cleaner(a.model, a.rise, a.hop);
  const auto input = Fixture(1536);
  AudioLevel::PcmHop output, reference;
  Check(cleaner.Warmup({}), "mode warmup failed");
  OfflineCleaner offline(a.model, a.rise, a.hop);
  offline.ProcessPcm16(input, true, reference);
  const auto info = cleaner.Process(input, true, output);
  Check(output == reference && info.mode == ProcessingMode::Default &&
        info.voice_q12 == 4096 && info.music_q12 == 1295 && info.other_q12 == 410,
        "initial Default differs from verified offline first-hop behavior");
  const auto segment = cleaner.Process(input, true, output);
  Check(segment.reset_neural_state && output == reference,
        "new live segment did not reset recurrent state/synthesis/level");

  cleaner.Configure({-3, -16, 2, 65, 3});
  cleaner.SetMode(ProcessingMode::Default);
  auto controls = cleaner.Controls();
  Check(controls.effective.voice_db == 0 && controls.effective.music_db == -10 &&
        controls.effective.other_db == -20 && controls.effective.reduction_percent == 100 &&
        controls.effective.boost_db == 0 && controls.custom.voice_db == -3 && controls.custom.boost_db == 3,
        "Default overwrote Custom or kept its boost/reduction");
  cleaner.SetMode(ProcessingMode::Off);
  Check(cleaner.Controls().custom.music_db == -16, "Off overwrote custom gains");
  Check(cleaner.Warmup({}), "Off warmup failed");
  // All 65536 PCM codes, independently assembled dry history across packets.
  std::vector<std::int16_t> full(40 * 1920 + 1152);
  for (std::size_t i = 0; i < full.size(); ++i)
    full[i] = static_cast<std::int16_t>(static_cast<int>(i % 65536) - 32768);
  std::uint64_t mismatch = 0;
  for (std::size_t i = 0; i < 40; ++i) {
    const auto packet = std::span(full).subspan(i == 0 ? 0 : i * 1920 + 1152, i == 0 ? 3072 : 1920);
    const auto result = cleaner.Process(packet, i == 0, output);
    Check(result.work == CleanerWork::Bypass && result.mode == ProcessingMode::Off, "Off ran AI");
    for (std::size_t j = 0; j < output.size(); ++j) mismatch += output[j] != full[i * 1920 + j];
  }
  const auto state = cleaner.ModelStateSnapshot();
  Check(mismatch == 0 && cleaner.Snapshot().ai_runs == 0 && cleaner.Snapshot().off_packets == 40 &&
        std::all_of(state.begin(), state.end(), [](float x) { return x == 0; }),
        "Off was not original-only with unchanged/reset neural state");
  cleaner.SetMode(ProcessingMode::Custom);
  controls = cleaner.Controls();
  Check(controls.effective.voice_db == -3 && controls.effective.music_db == -16 &&
        controls.effective.other_db == 2 && controls.effective.reduction_percent == 65 &&
        controls.effective.boost_db == 3, "Custom did not survive Default and Off");
  auto changed = cleaner.Process(std::span(input).first(1920), false, output);
  Check(changed.mode == ProcessingMode::Custom && changed.reset_neural_state &&
        changed.voice_q12 == OfflineCleaner::DbToQ12(-3) &&
        changed.music_q12 == OfflineCleaner::DbToQ12(-16) &&
        changed.other_q12 == OfflineCleaner::DbToQ12(2), "custom publication/reset failed");

  // A new Custom stream has the same first-hop gains/level contract as Step 4.
  Check(cleaner.Warmup({}), "custom warmup failed");
  OfflineCleaner custom_reference(a.model, a.rise, a.hop);
  custom_reference.SetSourceMixDb(-3, -16, 2);
  custom_reference.SetReductionPercent(65); custom_reference.SetVoiceBoostDb(3);
  custom_reference.ProcessPcm16(input, true, reference);
  cleaner.Process(input, true, output);
  Check(output == reference, "Custom differs from the verified offline gains/level contract");
  cleaner.Configure({-101, 11, 11, 150, -1});
  controls = cleaner.Controls();
  Check(controls.custom.voice_db == -100 && controls.custom.music_db == 10 &&
        controls.custom.other_db == 10 && controls.custom.reduction_percent == 100 &&
        controls.custom.boost_db == 0, "custom limits failed");
  MustFail([&] { cleaner.SetMode(static_cast<ProcessingMode>(99)); }, "invalid mode was accepted");
  report << "modes=PASS Default_and_Custom_first_hop_offline_PCM_error=0"
         << " Off_all_PCM_codes_timeline_mismatches=" << mismatch
         << " Off_packets=40 Off_AI_runs=0 custom_survives_Default_Off=PASS\n";
}

struct Record {
  AudioLevel::PcmHop dry, output;
  ProcessInfo info;
};
class RecordingProcessor final : public PacketProcessor {
 public:
  explicit RecordingProcessor(const Assets& a) : cleaner(a.model, a.rise, a.hop) { records.reserve(1200); }
  bool Warmup(std::stop_token stop) override { history.fill(0); records.clear(); return cleaner.Warmup(stop); }
  ProcessInfo Process(std::span<const std::int16_t> input, bool first, AudioLevel::PcmHop& out) override {
    Check(records.size() < records.capacity(), "bounded diagnostic trace exhausted");
    Record record;
    if (first) std::copy_n(input.begin(), 1920, record.dry.begin());
    else {
      std::copy(history.begin(), history.end(), record.dry.begin());
      std::copy_n(input.begin(), 768, record.dry.begin() + 1152);
    }
    std::copy(input.end() - 1152, input.end(), history.begin());
    record.info = cleaner.Process(input, first, out);
    record.output = out; records.push_back(record); return record.info;
  }
  RealtimeCleaner cleaner;
  std::vector<Record> records; // read only after the workers join
 private: std::array<std::int16_t, 1152> history{};
};
class TappedRender final : public StreamOutput {
 public:
  explicit TappedRender(const std::wstring& id) : native(id) { delivered.reserve(1200 * 1920); }
  void Open(std::stop_token stop) override { delivered.clear(); native.Open(stop); }
  void Close() noexcept override { native.Close(); }
  void Finish(std::stop_token stop) override { native.Finish(stop); }
  std::size_t Write(std::span<const std::int16_t> source, std::uint64_t frame, std::stop_token stop) override {
    if (fault_after && frame >= fault_after) throw std::runtime_error("injected output-device failure (controlled)");
    Check(frame * 2 == delivered.size(), "render tap timeline mismatch");
    Check(delivered.size() + source.size() <= delivered.capacity(), "bounded render trace exhausted");
    const auto frames = native.Write(source, frame, stop);
    delivered.insert(delivered.end(), source.begin(), source.begin() + frames * 2);
    return frames;
  }
  WasapiRender native;
  std::vector<std::int16_t> delivered;
  std::uint64_t fault_after = 0;
};

class SegmentInput final : public StreamInput {
 public:
  SegmentInput() : first(Fixture(3 * 960 + 576 + 200)), second(Fixture(13 * 960 + 576)) {
    for (auto& sample : second) sample = static_cast<std::int16_t>(sample / 2 + 73);
  }
  ReadResult Read(std::span<std::int16_t> destination, std::stop_token) override {
    bool discontinuity = false;
    if (!in_second && cursor == first.size()) { in_second = true; cursor = 0; discontinuity = true; }
    const auto& source = in_second ? second : first;
    if (cursor == source.size()) return {0, ReadStatus::End};
    const auto frames = std::min({std::size_t{37}, destination.size() / 2, (source.size() - cursor) / 2});
    std::copy_n(source.begin() + cursor, frames * 2, destination.begin()); cursor += frames * 2;
    return {frames, ReadStatus::Data, discontinuity};
  }
  std::vector<std::int16_t> first, second;
 private: std::size_t cursor = 0; bool in_second = false;
};
class SegmentOutput final : public StreamOutput {
 public:
  SegmentOutput() { samples.reserve(16 * 1920); }
  std::size_t Write(std::span<const std::int16_t> source, std::uint64_t frame, std::stop_token) override {
    Check(frame * 2 == samples.size(), "segmented output timeline was doubled/gapped");
    const auto frames = std::min(std::size_t{97}, source.size() / 2);
    samples.insert(samples.end(), source.begin(), source.begin() + frames * 2); return frames;
  }
  std::vector<std::int16_t> samples;
};
void TestSegments(const Assets& a, std::ostringstream& report) {
  auto processor = std::make_shared<RecordingProcessor>(a); processor->cleaner.SetMode(ProcessingMode::Off);
  auto input = std::make_shared<SegmentInput>(); auto output = std::make_shared<SegmentOutput>();
  BufferedAudioPipeline pipeline(processor, input, output);
  pipeline.Start(); Check(pipeline.WaitStopped(3s), "segmented deterministic stream stalled");
  auto expected = std::vector<std::int16_t>(input->first.begin(), input->first.begin() + 3 * 1920);
  expected.insert(expected.end(), input->second.begin(), input->second.begin() + 13 * 1920);
  const auto p = pipeline.Snapshot();
  Check(p.failure.empty() && p.played == 16 && output->samples == expected && p.capture_segments_reset == 1 &&
        p.discarded_partial_frames == 200 && p.discarded_future_context_frames == 576 &&
        processor->records[3].info.reset_neural_state && p.free_inputs == 5 && p.free_outputs == 5,
        "discontinuity did not discard old context/reset/retain exact original segment order");
  report << "controlled_capture_discontinuity=PASS segment_resets=1 discarded_partial_frames=200"
         << " discarded_future_context_frames=576 Off_PCM_error=0 played_packets=16 pools=5/5\n";
}

void PrintPipeline(std::ostringstream& out, const char* label, const PipelineSnapshot& p,
                   const RecoverySnapshot& r, const WasapiSnapshot& capture, const WasapiSnapshot& render,
                   double shutdown_ms) {
  out << label << " captured=" << p.captured << " processed=" << p.processed << " played=" << p.played
      << " queue_high_water=" << p.captured_high_water << '/' << p.rendered_high_water
      << " final_queues=" << p.captured_queue << '/' << p.rendered_queue
      << " ai_runs=" << r.ai_runs << " off=" << r.off_packets << " fallbacks=" << r.fallbacks
      << " recoveries=" << r.recoveries << " probes=" << r.probes << " probe_failures=" << r.probe_failures
      << " engine_p50_p95_p99_ms=" << p.ai_engine.p50_ms << '/' << p.ai_engine.p95_ms << '/' << p.ai_engine.p99_ms
      << " engine_mean_max_ms=" << p.ai_engine.mean_ms << '/' << p.ai_engine.max_ms
      << " engine_over20=" << p.ai_engine.over_budget << '/' << p.ai_engine.count
      << " packet_p50_p95_p99_ms=" << p.ai_packet.p50_ms << '/' << p.ai_packet.p95_ms << '/' << p.ai_packet.p99_ms
      << " packet_max_ms=" << p.ai_packet.max_ms << " packet_over20=" << p.ai_packet.over_budget
      << " bypass_p99_ms=" << p.bypass_packet.p99_ms
      << " captured_frames=" << capture.frames << " submitted_render_frames=" << render.frames
      << " native_buffer_frames=" << capture.buffer_frames << '/' << render.buffer_frames
      << " startup_discontinuities=" << capture.startup_discontinuities << " discontinuities=" << capture.discontinuities
      << " segment_resets=" << p.capture_segments_reset << " discarded_partial_frames=" << p.discarded_partial_frames
      << " discarded_future_context_frames=" << p.discarded_future_context_frames
      << " mmcss_registered=" << capture.mmcss_registered << '/' << render.mmcss_registered
      << " timestamp_errors=" << capture.timestamp_errors << " wait_timeouts=" << capture.wait_timeouts << '/' << render.wait_timeouts
      << " empty_render_observations=" << render.empty_render_observations << " shutdown_ms=" << shutdown_ms
      << " error=" << p.failure << '\n';
}

double Amplitude(const std::vector<std::int16_t>& samples, std::size_t first, unsigned channel, double frequency) {
  constexpr std::size_t frames = 9600;
  double real = 0, imaginary = 0;
  for (std::size_t i = 0; i < frames; ++i) {
    const double angle = 2 * 3.14159265358979323846 * frequency * i / 48000;
    const double value = samples[(first + i) * 2 + channel];
    real += value * std::cos(angle); imaginary += value * std::sin(angle);
  }
  return 2 * std::hypot(real, imaginary) / frames;
}

void TestLive(const Assets& a, const AudioRoute& route, unsigned source_rate, std::ostringstream& report) {
  auto processor = std::make_shared<RecordingProcessor>(a);
  processor->cleaner.SetMode(ProcessingMode::Off);
  auto input = std::make_shared<WasapiCapture>(route.capture_id, 48000, true);
  auto output = std::make_shared<TappedRender>(route.physical_render_id);
  PipelineOptions options; options.schedule_processing_audio = true;
  BufferedAudioPipeline pipeline(processor, input, output, options);
  const std::wstring label = L"live-" + std::to_wstring(source_rate);
  pipeline.Start();
  Check(WaitUntil([&] { return input->Snapshot().open || !pipeline.Snapshot().failure.empty(); }), "native capture startup timeout");
  SourceProcess source(a, route, source_rate, 9, label);
  auto Phase = [&](std::uint64_t packets, ProcessingMode mode) {
    processor->cleaner.SetMode(mode);
    const auto target = pipeline.Snapshot().processed + packets;
    Check(WaitUntil([&] { const auto p = pipeline.Snapshot(); return !p.failure.empty() || p.processed >= target; }, 4s),
          "live phase processing deadline exceeded");
    const auto snapshot = pipeline.Snapshot();
    if (!snapshot.failure.empty()) {
      PrintPipeline(report, "failed_live_phase", snapshot, processor->cleaner.Snapshot(),
                    input->Snapshot(), output->native.Snapshot(), 0);
      throw std::runtime_error(snapshot.failure);
    }
  };
  Phase(80, ProcessingMode::Off);
  Phase(90, ProcessingMode::Default);
  processor->cleaner.Configure({-3, -16, -20, 100, 0});
  Phase(75, ProcessingMode::Custom);
  Phase(45, ProcessingMode::Default);
  Phase(45, ProcessingMode::Off);
  Phase(45, ProcessingMode::Custom);
  Phase(25, ProcessingMode::Off);
  source.Wait(5);
  std::this_thread::sleep_for(100ms);
  const auto cancel = Clock::now(); pipeline.RequestStop();
  Check(pipeline.WaitStopped(3s), "live worker shutdown timeout");
  const double shutdown = std::chrono::duration<double, std::milli>(Clock::now() - cancel).count();
  const auto p = pipeline.Snapshot(); const auto r = processor->cleaner.Snapshot();
  const auto capture = input->Snapshot(), render = output->native.Snapshot();
  PrintPipeline(report, Utf8(label).c_str(), p, r, capture, render, shutdown);
  Check(p.failure.empty() && !capture.open && !render.open, "native live stream failed or leaked open clients");
  Check(p.capture_first_read_order > p.playback_ready_order && p.capture_first_read_order > p.process_ready_order,
        "live consumer-first startup failed");
  Check(p.captured_high_water <= 3 && p.rendered_high_water <= 3 && p.processed >= 400,
        "live stream did not remain bounded or process enough hops");
  std::uint64_t off_mismatches = 0, changed_on_samples = 0, off_nonzero = 0, default_packets = 0, custom_packets = 0;
  std::vector<std::int16_t> dry, processed;
  dry.reserve(processor->records.size() * 1920); processed.reserve(dry.capacity());
  for (const auto& record : processor->records) {
    dry.insert(dry.end(), record.dry.begin(), record.dry.end());
    processed.insert(processed.end(), record.output.begin(), record.output.end());
    if (record.info.mode == ProcessingMode::Off) {
      for (std::size_t i = 0; i < 1920; ++i) {
        off_mismatches += record.output[i] != record.dry[i]; off_nonzero += record.dry[i] != 0;
      }
      Check(record.info.work == CleanerWork::Bypass, "live Off ran inference");
    } else {
      if (record.info.mode == ProcessingMode::Default) {
        ++default_packets;
        Check(record.info.voice_q12 == 4096 && record.info.music_q12 == 1295 && record.info.other_q12 == 410,
              "live Default gains were not atomic/default");
      } else {
        ++custom_packets;
        Check(record.info.voice_q12 == OfflineCleaner::DbToQ12(-3) &&
              record.info.music_q12 == OfflineCleaner::DbToQ12(-16) && record.info.other_q12 == 410,
              "live Custom gains were torn or not preserved");
      }
      if (record.info.work != CleanerWork::Bypass)
        for (std::size_t i = 0; i < 1920; ++i) changed_on_samples += record.output[i] != record.dry[i];
    }
  }
  Check(off_mismatches == 0 && off_nonzero > 10000 && changed_on_samples > 10000 &&
        default_packets > 80 && custom_packets > 80, "live Off/On evidence is absent or incorrect");
  Check(output->delivered.size() <= processed.size() &&
        std::equal(output->delivered.begin(), output->delivered.end(), processed.begin()),
        "physical output differs from the single processed stream or duplicates frames");
  Check(render.frames * 2 == output->delivered.size(), "native submitted frame accounting mismatch");
  const double left = Amplitude(dry, 20 * 960, 0, 337), right = Amplitude(dry, 20 * 960, 1, 719);
  const double wrong_left = Amplitude(dry, 20 * 960, 0, 719), wrong_right = Amplitude(dry, 20 * 960, 1, 337);
  Check(left > 10 && right > 10 && wrong_left < left * .1 && wrong_right < right * .1,
        "live source did not preserve stereo channel tones");
  const auto custom = processor->cleaner.Controls().custom;
  Check(custom.voice_db == -3 && custom.music_db == -16 && custom.other_db == -20, "live Custom mix was lost");
  if (a.save_audio) {
    WritePcm16StereoWav(a.evidence / (label + L"-aligned-original.wav"), {48000, std::move(dry)});
    WritePcm16StereoWav(a.evidence / (label + L"-submitted-output.wav"), {48000, output->delivered});
  }
  report << Utf8(label) << " RESULT=PASS Off_mismatches=" << off_mismatches << " Off_nonzero_samples=" << off_nonzero
         << " changed_On_samples=" << changed_on_samples << " Default_packets=" << default_packets
         << " Custom_packets=" << custom_packets << " single_physical_stream_PCM_error=0"
         << " stereo_tone_amplitudes=" << left << '/' << right << " wrong_channel=" << wrong_left << '/' << wrong_right
         << " source_client_rate=" << source_rate << " pipeline_client_rate=48000 capture_mix_rate=" << capture.mix_rate
         << " physical_mix_rate=" << render.mix_rate << '\n';
}

void TestOutputFault(const Assets& a, const AudioRoute& route, std::ostringstream& report) {
  auto processor = std::make_shared<RecordingProcessor>(a);
  auto input = std::make_shared<WasapiCapture>(route.capture_id, 48000, true);
  auto output = std::make_shared<TappedRender>(route.physical_render_id);
  output->fault_after = 30 * 960;
  PipelineOptions options; options.schedule_processing_audio = true;
  BufferedAudioPipeline pipeline(processor, input, output, options);
  pipeline.Start();
  SourceProcess source(a, route, 48000, 2, L"injected-fault");
  Check(pipeline.WaitStopped(4s), "output failure did not stop native workers");
  const auto p = pipeline.Snapshot();
  Check(p.failure.find("injected output-device failure") != std::string::npos &&
        !input->Snapshot().open && !output->native.Snapshot().open,
        "controlled output error was lost or left native endpoints open");
  source.Wait(4);
  // Same processor + native adapters, explicit fresh stream, no implicit switch.
  output->fault_after = 0; processor->cleaner.SetMode(ProcessingMode::Off);
  pipeline.Start();
  Check(WaitUntil([&] { return pipeline.Snapshot().processed >= 20 || !pipeline.Snapshot().failure.empty(); }),
        "explicit recovery restart stalled");
  pipeline.RequestStop(); Check(pipeline.WaitStopped(3s), "restart cleanup failed");
  Check(pipeline.Snapshot().failure.empty() && processor->cleaner.Snapshot().ai_runs == 0 &&
        !input->Snapshot().open && !output->native.Snapshot().open, "fresh restart retained fault/state");
  report << "controlled_output_failure=PASS first_error_preserved=PASS native_clients_closed=PASS"
         << " explicit_restart_same_endpoints=PASS clean_Off_state=PASS fault_played_packets=" << p.played << '\n';
}

void TestController(const Assets& a, const AudioRoute& route, std::ostringstream& report) {
  auto invalid = route; invalid.physical_render_id = L"missing selected output";
  LiveSession session(a.model, a.rise, a.hop, invalid);
  MustFail([&] { session.Start(); }, "controller accepted unavailable selected output");
  Check(session.Snapshot().state == LiveState::Faulted && !session.Snapshot().error.empty(),
        "controller did not preserve output selection error");
  session.SelectRoute(route); session.Configure({-4, -15, -19, 70, 2}); session.SetMode(ProcessingMode::Off);
  double maximum = 0;
  for (int i = 0; i < 3; ++i) {
    session.Start();
    Check(WaitUntil([&] { auto s = session.Snapshot(); return s.pipeline.processed >= 20 || s.state == LiveState::Faulted; }),
          "controller live start stalled");
    auto snapshot = session.Snapshot();
    Check(snapshot.error.empty() && snapshot.cleaner.ai_runs == 0 && snapshot.generation == static_cast<unsigned>(i + 1),
          "controller restart failed to clear fault or reset timing");
    MustFail([&] { session.SelectRoute(invalid); }, "controller changed output during live run");
    const auto begin = Clock::now(); Check(session.Stop(), "controller Stop failed");
    maximum = std::max(maximum, std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
    snapshot = session.Snapshot();
    Check(snapshot.state == LiveState::Stopped && !snapshot.capture.open && !snapshot.render.open &&
          snapshot.controls.custom.voice_db == -4 && snapshot.controls.custom.boost_db == 2,
          "controller stop/restart lost saved mix or native client cleanup");
  }
  WriteLiveStatus(a.evidence / L"controller-final-status.json", session.Snapshot());
  report << "controller=PASS unavailable_selected_output_fault=PASS no_implicit_default_follow=PASS"
         << " explicit_output_reselection=PASS native_start_stop_cycles=3 Custom_preserved=PASS max_stop_ms=" << maximum << '\n';
}

int Run(const Assets& a, const AudioRoute& route) {
  std::filesystem::create_directories(a.evidence);
  std::ostringstream report;
  report << "CleanWave Step 7 real live integration verification\n"
         << "route=independent_source_process_WASAPI_CABLE_Input->CABLE_Output_capture->audio_AI->selected_physical_WASAPI\n"
         << "loopback_capture=absent system_setting_setters=absent synthetic_source=bounded_low_level_stereo\n";
  int result = 0;
  try {
    const auto before = EnumerateAudioEndpoints(); ValidateAudioRoute(route, before);
    report << "physical_output=" << Utf8(InspectAudioEndpoint(route.physical_render_id).name)
           << " endpoint_id=" << Utf8(route.physical_render_id) << '\n';
    TestModes(a, report); TestSegments(a, report);
    TestLive(a, route, 48000, report); TestLive(a, route, 44100, report);
    TestOutputFault(a, route, report); TestController(a, route, report);
    const auto after = EnumerateAudioEndpoints();
    for (const auto& old : before) {
      const auto current = std::find_if(after.begin(), after.end(), [&](const auto& item) { return item.id == old.id; });
      Check(current != after.end() && current->state == old.state && current->default_console == old.default_console &&
            current->default_multimedia == old.default_multimedia && current->default_communications == old.default_communications &&
            current->mix_rate == old.mix_rate && current->channels == old.channels && current->bits == old.bits &&
            current->muted == old.muted && std::abs(current->volume - old.volume) < .00001F,
            "endpoint settings changed during live verification");
    }
    report << "endpoint_defaults_formats_mute_volume_unchanged=PASS\n"
           << "unrun=consumer_app_route_change unplug_suspend_service_restart subjective_listening endurance end_to_end_latency\n"
           << "empty_render_observations_are_not_audible_dropout_counts engine_time_is_not_end_to_end_latency\nRESULT=PASS\n";
  } catch (const std::exception& e) { report << "FAILURE=" << e.what() << "\nRESULT=FAIL\n"; result = 1; }
  std::ofstream(a.evidence / "live-verification.txt") << report.str();
  std::cout << report.str(); return result;
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 9 && !(argc == 10 && std::wstring(argv[9]) == L"--no-wav")) {
    std::cerr << "usage: live_verify model rise hop source-id capture-id physical-id source-exe evidence-dir [--no-wav]\n"; return 2;
  }
  try { return Run({argv[1], argv[2], argv[3], argv[7], argv[8], argc == 9}, {argv[4], argv[5], argv[6]}); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
