#include "audio/PipeWireBridge.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <utility>

#include <unistd.h>

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/utils/json.h>
#include <spa/utils/result.h>

namespace sukut::linux_audio {

struct PipeWireBridge::Hooks {
  spa_hook core{}, registry{}, metadata{}, sink_node{}, capture{}, playback{};
};

namespace {

constexpr const char* kConfiguredSink = "default.configured.audio.sink";
constexpr const char* kEffectiveSink = "default.audio.sink";
constexpr const char* kTargetObject = "target.object";
constexpr std::uint32_t kRingFrames = 16384;  // ~341 ms
constexpr std::uint32_t kStride = sizeof(std::int16_t) * kChannels;

class LoopLock {
 public:
  explicit LoopLock(pw_thread_loop* loop) : loop_(loop) { pw_thread_loop_lock(loop_); }
  ~LoopLock() { pw_thread_loop_unlock(loop_); }
  LoopLock(const LoopLock&) = delete;
  LoopLock& operator=(const LoopLock&) = delete;
 private:
  pw_thread_loop* loop_;
};

PipeWireBridge* Self(void* data) { return static_cast<PipeWireBridge*>(data); }

bool ProcessExists(std::uint32_t pid) { return pid != 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM); }

// WirePlumber stores default devices as JSON: { "name": "<node.name>" }.
std::string NameFromJson(const char* value) {
  if (value == nullptr) return {};
  spa_json outer{}, object{};
  spa_json_init(&outer, value, std::strlen(value));
  if (spa_json_enter_object(&outer, &object) <= 0) return {};
  char key[64];
  while (spa_json_get_string(&object, key, sizeof(key)) > 0) {
    if (std::strcmp(key, "name") == 0) {
      char name[512];
      return spa_json_get_string(&object, name, sizeof(name)) > 0 ? std::string(name) : std::string();
    }
    const char* skipped = nullptr;
    if (spa_json_next(&object, &skipped) <= 0) break;
  }
  return {};
}

std::string NameToJson(const std::string& name) {
  std::string escaped;
  for (char c : name) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }
  return "{ \"name\": \"" + escaped + "\" }";
}

const char* Lookup(const spa_dict* props, const char* key) { return spa_dict_lookup(props, key); }
std::uint64_t LookupNumber(const spa_dict* props, const char* key) {
  const char* value = Lookup(props, key);
  return value ? std::strtoull(value, nullptr, 10) : 0;
}

void RegistryGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
  Self(data)->OnGlobal(id, type, props);
}
void RegistryGlobalRemove(void* data, uint32_t id) { Self(data)->OnGlobalRemove(id); }
void CoreDone(void* data, uint32_t id, int seq) { Self(data)->OnCoreDone(id, seq); }
void CoreError(void* data, uint32_t id, int, int res, const char* message) { Self(data)->OnCoreError(id, res, message); }
int MetadataProperty(void* data, uint32_t subject, const char* key, const char*, const char* value) {
  Self(data)->OnMetadata(subject, key, value);
  return 0;
}
void NodeParam(void* data, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param) {
  if (id == SPA_PARAM_Props) Self(data)->OnSinkProps(param);
}
void CaptureProcess(void* data) { Self(data)->OnCaptureProcess(); }
void PlaybackProcess(void* data) { Self(data)->OnPlaybackProcess(); }
void CaptureState(void* data, pw_stream_state, pw_stream_state state, const char* error) {
  Self(data)->OnStreamState(false, state, error);
}
void PlaybackState(void* data, pw_stream_state, pw_stream_state state, const char* error) {
  Self(data)->OnStreamState(true, state, error);
}

const pw_registry_events kRegistryEvents = [] {
  pw_registry_events e{}; e.version = PW_VERSION_REGISTRY_EVENTS;
  e.global = RegistryGlobal; e.global_remove = RegistryGlobalRemove; return e;
}();
const pw_core_events kCoreEvents = [] {
  pw_core_events e{}; e.version = PW_VERSION_CORE_EVENTS; e.done = CoreDone; e.error = CoreError; return e;
}();
const pw_metadata_events kMetadataEvents = [] {
  pw_metadata_events e{}; e.version = PW_VERSION_METADATA_EVENTS; e.property = MetadataProperty; return e;
}();
const pw_node_events kNodeEvents = [] {
  pw_node_events e{}; e.version = PW_VERSION_NODE_EVENTS; e.param = NodeParam; return e;
}();
const pw_stream_events kCaptureEvents = [] {
  pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
  e.process = CaptureProcess; e.state_changed = CaptureState; return e;
}();
const pw_stream_events kPlaybackEvents = [] {
  pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
  e.process = PlaybackProcess; e.state_changed = PlaybackState; return e;
}();

}  // namespace

PipeWireBridge::PipeWireBridge() : hooks_(std::make_unique<Hooks>()) {}
PipeWireBridge::~PipeWireBridge() { Stop(); Disconnect(); }

// ----- connection -----------------------------------------------------------

bool PipeWireBridge::Connect(std::string& error) {
  pw_init(nullptr, nullptr);
  loop_ = pw_thread_loop_new("sukut-pipewire", nullptr);
  if (loop_ == nullptr) { error = "cannot create the PipeWire loop"; return false; }
  // No real-time data thread (client.conf's module-rt condition). On the stage 9
  // machine, sukut-host was killed (SIGKILL) within 0.2 s of PipeWire making
  // data-loop.0 SCHED_RR through RealtimeKit, though that thread used ~30 ms of
  // CPU in 5.6 s without it. The process callbacks only copy (PLAN section 5.4);
  // the processing thread keeps its own RealtimeKit priority (nice -11).
  context_ = pw_context_new(pw_thread_loop_get_loop(loop_), pw_properties_new("module.rt", "false", nullptr), 0);
  if (context_ == nullptr || pw_thread_loop_start(loop_) < 0) { error = "cannot start the PipeWire loop"; return false; }
  LoopLock lock(loop_);
  core_ = pw_context_connect(context_, nullptr, 0);
  if (core_ == nullptr) { error = "cannot connect to PipeWire; is it running?"; return false; }
  pw_core_add_listener(core_, &hooks_->core, &kCoreEvents, this);
  registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(registry_, &hooks_->registry, &kRegistryEvents, this);
  // First roundtrip lists the globals; the second delivers the metadata values.
  if (!Roundtrip() || !Roundtrip()) { error = "PipeWire did not answer"; return false; }
  if (metadata_ == nullptr) { error = "WirePlumber's \"default\" metadata is missing; is WirePlumber running?"; return false; }
  return true;
}

void PipeWireBridge::Disconnect() {
  if (loop_ == nullptr) return;
  pw_thread_loop_stop(loop_);
  if (metadata_ != nullptr) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(metadata_)); metadata_ = nullptr; }
  if (registry_ != nullptr) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_)); registry_ = nullptr; }
  if (core_ != nullptr) { pw_core_disconnect(core_); core_ = nullptr; }
  if (context_ != nullptr) { pw_context_destroy(context_); context_ = nullptr; }
  pw_thread_loop_destroy(loop_);
  loop_ = nullptr;
  nodes_.clear();
}

bool PipeWireBridge::Roundtrip() {
  done_ = false;
  pending_seq_ = pw_core_sync(core_, PW_ID_CORE, pending_seq_);
  while (!done_)
    if (pw_thread_loop_timed_wait(loop_, 3) != 0) return false;
  return true;
}

bool PipeWireBridge::WaitForNode(const std::string& name, int attempts) {
  for (int i = 0; i < attempts; ++i) {
    for (const auto& [id, node] : nodes_)
      if (node.name == name) return true;
    if (!Roundtrip()) return false;
  }
  return false;
}

// ----- events (PipeWire loop thread, lock held) ------------------------------

void PipeWireBridge::OnGlobal(std::uint32_t id, const char* type, const spa_dict* props) {
  if (props == nullptr) return;
  if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char* name = Lookup(props, PW_KEY_NODE_NAME);
    const char* media_class = Lookup(props, PW_KEY_MEDIA_CLASS);
    const char* description = Lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char* app = Lookup(props, PW_KEY_APP_NAME);
    Node node{name ? name : "", media_class ? media_class : "", description ? description : "", app ? app : "",
              LookupNumber(props, PW_KEY_OBJECT_SERIAL), static_cast<std::uint32_t>(LookupNumber(props, PW_KEY_CLIENT_ID)),
              static_cast<int>(LookupNumber(props, PW_KEY_PRIORITY_SESSION))};
    nodes_[id] = node;
    // Follow the volume and mute set on Sukut's sink (volume keys, sound settings).
    if (node.name == kSinkName && sink_node_ == nullptr) {
      sink_node_ = static_cast<pw_node*>(pw_registry_bind(registry_, id, type, PW_VERSION_NODE, 0));
      if (sink_node_ != nullptr) {
        pw_node_add_listener(sink_node_, &hooks_->sink_node, &kNodeEvents, this);
        uint32_t ids[] = {SPA_PARAM_Props};
        pw_node_subscribe_params(sink_node_, ids, 1);
      }
    }
    // A new stream from the chosen app, or a first output after all were removed.
    if (routed_ && IsSourceStream(node)) TargetStream(id);
    else if (capture_ != nullptr && output_.empty() && IsOutput(node.name)) Retarget(node.name, "appeared");
  } else if (std::strcmp(type, PW_TYPE_INTERFACE_Client) == 0) {
    // The server-verified process id first; the app's own claim as fallback.
    std::uint64_t pid = LookupNumber(props, PW_KEY_SEC_PID);
    if (pid == 0) pid = LookupNumber(props, PW_KEY_APP_PROCESS_ID);
    clients_[id] = static_cast<std::uint32_t>(pid);
  } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && metadata_ == nullptr) {
    const char* name = Lookup(props, PW_KEY_METADATA_NAME);
    if (name != nullptr && std::strcmp(name, "default") == 0) {
      metadata_ = static_cast<pw_metadata*>(pw_registry_bind(registry_, id, type, PW_VERSION_METADATA, 0));
      if (metadata_ != nullptr) pw_metadata_add_listener(metadata_, &hooks_->metadata, &kMetadataEvents, this);
    }
  }
}

void PipeWireBridge::OnGlobalRemove(std::uint32_t id) {
  clients_.erase(id);
  const auto found = nodes_.find(id);
  if (found == nodes_.end()) return;
  const Node removed = found->second;
  nodes_.erase(found);
  if (const auto target = std::find(targets_.begin(), targets_.end(), id); target != targets_.end()) {
    targets_.erase(target);
    journal_.targets = targets_;
    SaveJournal();
    NoteSourceGone();
  }
  if (capture_ != nullptr && !removed.name.empty() && removed.name == output_ && removed.media_class == "Audio/Sink") {
    const std::string next = NextOutput(removed.name);
    if (journal_.previous_sink == removed.name && !next.empty()) { journal_.previous_sink = next; SaveJournal(); }
    if (next.empty()) {
      output_.clear();
      Event("OUTPUT_LOST");
    } else {
      Retarget(next, "removed");
    }
  }
}

void PipeWireBridge::OnMetadata(std::uint32_t subject, const char* key, const char* value) {
  if (subject != PW_ID_CORE) return;
  if (key == nullptr) { configured_sink_.clear(); effective_sink_.clear(); return; }
  if (std::strcmp(key, kEffectiveSink) == 0) { effective_sink_ = NameFromJson(value); return; }
  if (std::strcmp(key, kConfiguredSink) != 0) return;
  configured_sink_ = NameFromJson(value);
  if (capture_ == nullptr || !IsOutput(configured_sink_) || configured_sink_ == output_) return;
  // The user picked another output while Sukut runs: play there, keep Sukut
  // in front of it, and make it the output restored on Off.
  if (switched_default_) {
    journal_.previous_sink = configured_sink_;
    SaveJournal();
    Retarget(configured_sink_, "default");
    SetConfiguredSink(kSinkName);
  } else if (source_pid_ != 0) {
    Retarget(configured_sink_, "default");
  }
}

void PipeWireBridge::OnCoreDone(std::uint32_t id, int seq) {
  if (id == PW_ID_CORE && seq == pending_seq_) {
    done_ = true;
    pw_thread_loop_signal(loop_, false);
  }
}

void PipeWireBridge::OnCoreError(std::uint32_t, int res, const char* message) {
  stream_error_ = std::string(message ? message : "PipeWire error") + " (" + spa_strerror(res) + ")";
}

void PipeWireBridge::OnStreamState(bool playback, int state, const char* error) {
  if (state == PW_STREAM_STATE_ERROR)
    stream_error_ = std::string(playback ? "playback: " : "capture: ") + (error ? error : "stream error");
  if (playback && (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING)) {
    playback_ready_ = true;
    ApplyVolume();  // a new playback stream starts at the sink's current volume
  }
}

void PipeWireBridge::OnSinkProps(const spa_pod* param) {
  if (param == nullptr || !spa_pod_is_object_type(param, SPA_TYPE_OBJECT_Props)) return;
  float values[SPA_AUDIO_MAX_CHANNELS];
  auto* object = reinterpret_cast<spa_pod_object*>(const_cast<spa_pod*>(param));
  spa_pod_prop* prop = nullptr;
  SPA_POD_OBJECT_FOREACH(object, prop) {
    if (prop->key == SPA_PROP_channelVolumes) {
      const uint32_t count = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, SPA_AUDIO_MAX_CHANNELS);
      if (count > 0) volumes_.assign(values, values + count);
    } else if (prop->key == SPA_PROP_mute) {
      if (spa_pod_get_bool(&prop->value, &mute_) == 0) have_mute_ = true;
    }
  }
  ApplyVolume();
}

// The capture is taken before the sink's volume (the model needs full level),
// so the same volume and mute are applied on Sukut's playback instead.
void PipeWireBridge::ApplyVolume() {
  if (!volumes_.empty()) volume_ = volumes_[0];
  if (have_mute_) muted_ = mute_;
  if (playback_ == nullptr || !playback_ready_) return;
  if (!volumes_.empty())
    pw_stream_set_control(playback_, SPA_PROP_channelVolumes, static_cast<uint32_t>(volumes_.size()), volumes_.data(), 0);
  if (have_mute_) {
    float value = mute_ ? 1.0F : 0.0F;
    pw_stream_set_control(playback_, SPA_PROP_mute, 1, &value, 0);
  }
}

// ----- audio (PipeWire data thread) -------------------------------------------

void PipeWireBridge::OnCaptureProcess() {
  pw_buffer* buffer = pw_stream_dequeue_buffer(capture_);
  if (buffer == nullptr) return;
  spa_data& data = buffer->buffer->datas[0];
  if (data.data != nullptr && data.chunk != nullptr) {
    const std::uint32_t offset = std::min(data.chunk->offset, data.maxsize);
    const std::uint32_t frames = std::min(data.chunk->size, data.maxsize - offset) / kStride;
    const auto* source = reinterpret_cast<const std::int16_t*>(static_cast<const std::uint8_t*>(data.data) + offset);
    const std::uint64_t at = capture_ring_.WriteIndex();
    const std::uint32_t kept = capture_ring_.Write({source, static_cast<std::size_t>(frames) * kChannels});
    if (kept < frames) {
      // The engine fell behind: frames after `at + kept` are lost. Mark where the
      // next frames start so the engine begins a fresh segment there.
      overruns_.fetch_add(1, std::memory_order_relaxed);
      gap_at_.store(at + kept, std::memory_order_release);
    }
    captured_.fetch_add(frames, std::memory_order_relaxed);
  }
  pw_stream_queue_buffer(capture_, buffer);
}

void PipeWireBridge::OnPlaybackProcess() {
  pw_buffer* buffer = pw_stream_dequeue_buffer(playback_);
  if (buffer == nullptr) return;
  spa_data& data = buffer->buffer->datas[0];
  if (data.data == nullptr || data.chunk == nullptr) { pw_stream_queue_buffer(playback_, buffer); return; }
  std::uint32_t frames = data.maxsize / kStride;
  if (buffer->requested != 0) frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, buffer->requested));
  auto* target = static_cast<std::int16_t*>(data.data);
  FrameRing& ring = engine_ ? playback_ring_ : capture_ring_;
  // Start only with a full prebuffer, like the Windows output's prefill. The
  // engine path then plays continuously; pass-through refills after a gap.
  if (!playing_.load(std::memory_order_relaxed) && ring.Fill() >= prebuffer_) playing_.store(true, std::memory_order_relaxed);
  std::uint32_t count = 0;
  if (playing_.load(std::memory_order_relaxed)) {
    count = ring.Read({target, static_cast<std::size_t>(frames) * kChannels});
    if (count < frames) {
      underruns_.fetch_add(1, std::memory_order_relaxed);
      if (!engine_) playing_.store(false, std::memory_order_relaxed);
    }
  }
  std::fill(target + static_cast<std::size_t>(count) * kChannels, target + static_cast<std::size_t>(frames) * kChannels, std::int16_t{0});
  data.chunk->offset = 0;
  data.chunk->stride = kStride;
  data.chunk->size = frames * kStride;
  played_.fetch_add(count, std::memory_order_relaxed);
  pw_stream_queue_buffer(playback_, buffer);
}

// ----- routing ------------------------------------------------------------------

bool PipeWireBridge::IsOutput(const std::string& name) const {
  if (name.empty() || name == kSinkName) return false;
  for (const auto& [id, node] : nodes_)
    if (node.name == name && node.media_class == "Audio/Sink") return true;
  return false;
}

std::uint32_t PipeWireBridge::ProcessOf(const Node& node) const {
  const auto found = clients_.find(node.client);
  return found == clients_.end() ? 0 : found->second;
}

bool PipeWireBridge::IsSourceStream(const Node& node) const {
  return source_pid_ != 0 && node.media_class == "Stream/Output/Audio" && ProcessOf(node) == source_pid_;
}

std::string PipeWireBridge::ResolveOutput(const std::string& requested) const {
  if (IsOutput(requested)) return requested;
  if (!requested.empty()) return {};  // an explicit choice that doesn't exist is an error
  if (IsOutput(effective_sink_)) return effective_sink_;
  if (IsOutput(configured_sink_)) return configured_sink_;
  return NextOutput({});
}

// The device WirePlumber would pick: its current default if real, else the
// highest session priority.
std::string PipeWireBridge::NextOutput(const std::string& excluded) const {
  if (effective_sink_ != excluded && IsOutput(effective_sink_)) return effective_sink_;
  const Node* best = nullptr;
  for (const auto& [id, node] : nodes_)
    if (node.name != excluded && IsOutput(node.name) && (best == nullptr || node.priority > best->priority)) best = &node;
  return best ? best->name : std::string();
}

void PipeWireBridge::SetConfiguredSink(const std::string& name) {
  if (name.empty()) pw_metadata_set_property(metadata_, PW_ID_CORE, kConfiguredSink, nullptr, nullptr);
  else pw_metadata_set_property(metadata_, PW_ID_CORE, kConfiguredSink, "Spa:String:JSON", NameToJson(name).c_str());
}

void PipeWireBridge::TargetStream(std::uint32_t id) {
  if (std::find(targets_.begin(), targets_.end(), id) != targets_.end()) return;
  std::uint64_t serial = 0;
  for (const auto& [node_id, node] : nodes_)
    if (node.name == kSinkName) serial = node.serial;
  if (serial == 0) return;
  targets_.push_back(id);
  journal_.targets = targets_;
  SaveJournal();  // before the change, so a crash can always undo it
  pw_metadata_set_property(metadata_, id, kTargetObject, "Spa:Id", std::to_string(serial).c_str());
  Event("SOURCE_TARGETED node=" + std::to_string(id));
}

// Move Sukut's playback to another real output by recreating its stream.
void PipeWireBridge::Retarget(const std::string& output, const char* reason) {
  if (output == output_ && playback_ != nullptr) return;
  output_ = output;
  if (playback_ != nullptr) { pw_stream_destroy(playback_); playback_ = nullptr; }
  playback_ready_ = false;
  std::string error;
  if (!CreateStream(true, output_, error)) { stream_error_ = error; return; }
  Event(std::string("OUTPUT_CHANGED to=") + output_ + " reason=" + reason);
}

bool PipeWireBridge::CreateStream(bool playback, const std::string& target, std::string& error) {
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, playback ? "Playback" : "Capture",
      PW_KEY_NODE_NAME, playback ? "sukut_playback" : "sukut_capture",
      PW_KEY_NODE_DESCRIPTION, playback ? "Sukut output" : "Sukut capture",
      PW_KEY_APP_NAME, "Sukut",
      PW_KEY_TARGET_OBJECT, target.c_str(),
      PW_KEY_NODE_DONT_RECONNECT, "true",
      PW_KEY_NODE_LATENCY, "960/48000",
      nullptr);
  if (!playback) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
  pw_stream* stream = pw_stream_new(core_, playback ? "Sukut output" : "Sukut capture", props);
  if (stream == nullptr) { error = "cannot create the PipeWire stream"; return false; }
  // Published before connecting: the process callbacks dequeue from these members.
  (playback ? playback_ : capture_) = stream;
  pw_stream_add_listener(stream, playback ? &hooks_->playback : &hooks_->capture,
                         playback ? &kPlaybackEvents : &kCaptureEvents, this);
  std::uint8_t pod[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod, sizeof(pod));
  spa_audio_info_raw info{};
  info.format = SPA_AUDIO_FORMAT_S16_LE;
  info.rate = kRate;
  info.channels = kChannels;
  info.position[0] = SPA_AUDIO_CHANNEL_FL;
  info.position[1] = SPA_AUDIO_CHANNEL_FR;
  const spa_pod* params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
  const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
  if (pw_stream_connect(stream, playback ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0) {
    error = "cannot connect the PipeWire stream";
    pw_stream_destroy(stream);
    (playback ? playback_ : capture_) = nullptr;
    return false;
  }
  return true;
}

// Windows rule: closing the chosen app ends the session; never start filtering
// other apps on its behalf. Its last stream may go a moment before the process,
// so this is checked again every second (CheckSource).
void PipeWireBridge::NoteSourceGone() {
  if (source_pid_ == 0 || !routed_ || source_closed_ || !targets_.empty() || ProcessExists(source_pid_)) return;
  source_closed_ = true;
  Event("SOURCE_CLOSED");
}

void PipeWireBridge::CheckSource() {
  if (loop_ == nullptr) return;
  LoopLock lock(loop_);
  NoteSourceGone();
}

void PipeWireBridge::SaveJournal() {
  std::string error;
  if (!WriteJournal(journal_, error)) Event("JOURNAL_FAILED " + error);
}

void PipeWireBridge::Event(std::string text) {
  std::lock_guard lock(events_mutex_);
  events_.push_back(std::move(text));
}

std::vector<std::string> PipeWireBridge::TakeEvents() {
  std::lock_guard lock(events_mutex_);
  return std::exchange(events_, {});
}

std::string PipeWireBridge::Output() const {
  if (loop_ == nullptr) return output_;
  LoopLock lock(loop_);
  return output_;
}

bool PipeWireBridge::Route(std::string& error) {
  if (loop_ == nullptr) { error = "not started"; return false; }
  LoopLock lock(loop_);
  return RouteLocked(error);
}

bool PipeWireBridge::RouteLocked(std::string& error) {
  if (routed_) return true;
  journal_ = {};
  journal_.previous_sink = configured_sink_;
  journal_.source_pid = source_pid_;
  if (source_pid_ != 0) {
    if (!WriteJournal(journal_, error)) return false;
    routed_ = true;
    for (const auto& [id, node] : nodes_)
      if (IsSourceStream(node)) TargetStream(id);
  } else {
    journal_.default_switched = true;
    if (!WriteJournal(journal_, error)) return false;
    routed_ = true;
    switched_default_ = true;
    SetConfiguredSink(kSinkName);
  }
  Roundtrip();
  return true;
}

void PipeWireBridge::RestoreRouting() {
  if (loop_ == nullptr) return;
  LoopLock lock(loop_);
  RestoreLocked();
}

void PipeWireBridge::RestoreLocked() {
  if (!routed_) return;
  routed_ = false;
  if (switched_default_) {
    switched_default_ = false;
    // Never undo a newer choice: restore only while the default is still Sukut's sink.
    if (configured_sink_ == kSinkName) SetConfiguredSink(journal_.previous_sink);
  }
  for (const auto id : targets_)
    if (nodes_.count(id) != 0) pw_metadata_set_property(metadata_, id, kTargetObject, nullptr, nullptr);
  targets_.clear();
  Roundtrip();
  RemoveJournal();
}

// A journal left by a session that did not stop cleanly (lock held).
void PipeWireBridge::UndoJournalLocked(const RoutingJournal& journal) {
  if (journal.default_switched && configured_sink_ == kSinkName) SetConfiguredSink(journal.previous_sink);
  for (const auto id : journal.targets) {
    const auto found = nodes_.find(id);
    // Node ids are reused: only release a stream that still belongs to that app.
    if (found != nodes_.end() && ProcessOf(found->second) == journal.source_pid && found->second.media_class == "Stream/Output/Audio")
      pw_metadata_set_property(metadata_, id, kTargetObject, nullptr, nullptr);
  }
  Roundtrip();
}

// ----- start / stop ---------------------------------------------------------------

bool PipeWireBridge::Start(const Options& options, std::string& error) {
  if (loop_ != nullptr) { error = "already started"; return false; }
  if (!Connect(error)) { Disconnect(); return false; }
  LoopLock lock(loop_);

  if (const auto left = ReadJournal()) { UndoJournalLocked(*left); RemoveJournal(); }

  source_pid_ = options.source_pid;
  if (source_pid_ != 0 && (!ProcessExists(source_pid_) || source_pid_ == static_cast<std::uint32_t>(::getpid()))) {
    error = "source app " + std::to_string(source_pid_) + " is not running";
    return false;
  }
  output_ = ResolveOutput(options.output);
  if (output_.empty()) {
    error = options.output.empty() ? "no audio output found" : "audio output \"" + options.output + "\" not found";
    return false;
  }

  pw_properties* sink = pw_properties_new(
      "factory.name", "support.null-audio-sink",
      PW_KEY_NODE_NAME, kSinkName,
      PW_KEY_NODE_DESCRIPTION, "Sukut",
      PW_KEY_MEDIA_CLASS, "Audio/Sink",
      "audio.position", "FL,FR",
      "audio.rate", "48000",
      "object.linger", "false",            // disappears with Sukut, so apps fall back by themselves
      "monitor.channel-volumes", "false",  // capture before volume: the model sees full level
      nullptr);
  sink_proxy_ = static_cast<pw_proxy*>(pw_core_create_object(core_, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &sink->dict, 0));
  pw_properties_free(sink);
  if (sink_proxy_ == nullptr || !WaitForNode(kSinkName, 20)) {
    error = "cannot create Sukut's virtual sink" + (stream_error_.empty() ? std::string() : ": " + stream_error_);
    return false;
  }

  engine_ = options.engine;
  capture_ring_.Reset(std::clamp<std::uint32_t>(options.capture_frames, kHopFrames, kRingFrames * 4));
  playback_ring_.Reset(std::clamp<std::uint32_t>(options.playback_frames, kHopFrames, kRingFrames * 4));
  FrameRing& played = engine_ ? playback_ring_ : capture_ring_;
  prebuffer_ = std::clamp<std::uint32_t>(options.prebuffer_frames, kHopFrames / 2, played.Capacity());
  playing_ = false;
  gap_at_ = kNoGap;
  if (!CreateStream(false, kSinkName, error) || !CreateStream(true, output_, error)) return false;
  Roundtrip();
  Roundtrip();
  if (!stream_error_.empty()) { error = stream_error_; return false; }
  return !options.route || RouteLocked(error);
}

void PipeWireBridge::Stop() {
  if (loop_ == nullptr) return;
  {
    LoopLock lock(loop_);
    // Hand the apps back to the real output before Sukut's sink disappears.
    RestoreLocked();
    if (capture_ != nullptr) { pw_stream_destroy(capture_); capture_ = nullptr; }
    if (playback_ != nullptr) { pw_stream_destroy(playback_); playback_ = nullptr; }
    if (sink_node_ != nullptr) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(sink_node_)); sink_node_ = nullptr; }
    if (sink_proxy_ != nullptr) { pw_proxy_destroy(sink_proxy_); sink_proxy_ = nullptr; }
    if (core_ != nullptr) Roundtrip();
  }
  Disconnect();
}

BridgeStats PipeWireBridge::Stats() const {
  BridgeStats stats;
  stats.captured_frames = captured_.load();
  stats.played_frames = played_.load();
  stats.underruns = underruns_.load();
  stats.overruns = overruns_.load();
  stats.fill_frames = (engine_ ? playback_ring_ : capture_ring_).Fill();
  stats.playing = playing_.load();
  stats.volume = volume_.load();
  stats.muted = muted_.load();
  return stats;
}

bool PipeWireBridge::ReadCatalog(Catalog& catalog, std::string& error) {
  PipeWireBridge bridge;
  if (!bridge.Connect(error)) { bridge.Disconnect(); return false; }
  {
    LoopLock lock(bridge.loop_);
    const auto own = static_cast<std::uint32_t>(::getpid());
    catalog = {};
    for (const auto& [id, node] : bridge.nodes_) {
      if (bridge.IsOutput(node.name)) {
        catalog.outputs.push_back({node.name, node.description.empty() ? node.name : node.description});
      } else if (node.media_class == "Stream/Output/Audio") {
        const std::uint32_t pid = bridge.ProcessOf(node);
        const bool listed = std::any_of(catalog.apps.begin(), catalog.apps.end(), [pid](const auto& a) { return a.pid == pid; });
        if (pid != 0 && pid != own && !listed) catalog.apps.push_back({pid, node.app.empty() ? node.name : node.app});
      }
    }
    catalog.default_output = bridge.NextOutput({});
  }
  bridge.Disconnect();
  return true;
}

bool PipeWireBridge::RestoreFromJournal(std::string& message) {
  const auto journal = ReadJournal();
  if (!journal) { message = "nothing to restore"; return true; }
  PipeWireBridge bridge;
  std::string error;
  if (!bridge.Connect(error)) { message = error; bridge.Disconnect(); return false; }
  {
    LoopLock lock(bridge.loop_);
    const bool default_is_sukut = bridge.configured_sink_ == kSinkName;
    bridge.UndoJournalLocked(*journal);
    if (journal->default_switched)
      message = default_is_sukut
          ? "default output restored to " + (journal->previous_sink.empty() ? std::string("automatic") : journal->previous_sink)
          : "default output was already changed; left as it is";
    else
      message = "released the app's streams";
    RemoveJournal();
  }
  bridge.Disconnect();
  return true;
}

}  // namespace sukut::linux_audio
