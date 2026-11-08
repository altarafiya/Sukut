#include "audio/PipeWireBridge.h"

#include <algorithm>
#include <cctype>
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
constexpr const char* kTargetNode = "target.node";
constexpr const char* kPlaybackName = "sukut_playback";
constexpr std::uint32_t kRingFrames = 16384;  // ~341 ms
constexpr std::uint32_t kStride = sizeof(std::int16_t) * kChannels;
constexpr std::size_t kWantedKept = 8;
// A physical output that comes back this soon was re-created (a profile
// change), not newly connected.
constexpr auto kRecreatedWithin = std::chrono::seconds(5);

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
std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// A real device (ALSA, Bluetooth), not a virtual sink such as EasyEffects'.
bool IsDevice(const PipeWireBridge::Node& node) { return node.media_class == "Audio/Sink" && node.device != 0; }

// A virtual sink: an effects app's or a loopback's input, or a null sink.
bool IsVirtualSink(const PipeWireBridge::Node& node) { return node.media_class == "Audio/Sink" && node.device == 0; }

// HDMI and DisplayPort outputs come and go with screens; Sukut never jumps to one by itself.
bool IsDisplay(const PipeWireBridge::Node& node) {
  const std::string text = Lower(node.name + " " + node.description);
  return text.find("hdmi") != std::string::npos || text.find("displayport") != std::string::npos;
}

void RegistryGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
  Self(data)->OnGlobal(id, type, props);
}
void RegistryGlobalRemove(void* data, uint32_t id) { Self(data)->OnGlobalRemove(id); }
void CoreDone(void* data, uint32_t id, int seq) { Self(data)->OnCoreDone(id, seq); }
void CoreError(void* data, uint32_t id, int, int res, const char* message) { Self(data)->OnCoreError(id, res, message); }
int MetadataProperty(void* data, uint32_t subject, const char* key, const char* type, const char* value) {
  Self(data)->OnMetadata(subject, key, type, value);
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
  clients_.clear();
  devices_.clear();
  links_.clear();
  pins_.clear();
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
              static_cast<int>(LookupNumber(props, PW_KEY_PRIORITY_SESSION)),
              static_cast<std::uint32_t>(LookupNumber(props, PW_KEY_DEVICE_ID))};
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
    // A new stream from the chosen app; a new, returning or first output; or a
    // stream whose pin arrived before it did.
    if (routed_ && IsSourceStream(node)) TargetStream(id);
    else if (capture_ != nullptr && IsOutput(node.name)) OnOutputAppeared(node);
    else if (pins_.count(id) != 0) ConsiderPinned(id);
  } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
    devices_[id] = capture_ != nullptr;  // a device that appears while Sukut runs was just connected
  } else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
    links_[id] = {static_cast<std::uint32_t>(LookupNumber(props, PW_KEY_LINK_OUTPUT_NODE)),
                  static_cast<std::uint32_t>(LookupNumber(props, PW_KEY_LINK_INPUT_NODE))};
    CheckLoop();
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
  devices_.erase(id);
  links_.erase(id);
  pins_.erase(id);
  released_.erase(id);
  if (redirected_.erase(id) != 0) {
    journal_.pins.erase(std::remove_if(journal_.pins.begin(), journal_.pins.end(), [id](const auto& p) { return p.node == id; }),
                        journal_.pins.end());
    SaveJournal();
  }
  const auto found = nodes_.find(id);
  if (found == nodes_.end()) return;
  const Node removed = found->second;
  nodes_.erase(found);
  if (IsDevice(removed)) {
    const auto now = Clock::now();
    for (auto it = removed_at_.begin(); it != removed_at_.end();)
      it = now - it->second > std::chrono::seconds(30) ? removed_at_.erase(it) : std::next(it);
    removed_at_[removed.name] = now;
  }
  if (const auto target = std::find(targets_.begin(), targets_.end(), id); target != targets_.end()) {
    targets_.erase(target);
    journal_.targets = targets_;
    SaveJournal();
    NoteSourceGone();
  }
  if (capture_ != nullptr && !removed.name.empty() && removed.name == output_ && removed.media_class == "Audio/Sink") {
    // The most recent output the user wanted that is still here, else the device WirePlumber would pick.
    std::string next = BestWanted(removed.name);
    if (next.empty()) next = NextOutput(removed.name);
    if (wanted_.empty() && journal_.previous_sink == removed.name && !next.empty()) { journal_.previous_sink = next; SaveJournal(); }
    if (next.empty()) {
      output_.clear();
      Event("OUTPUT_LOST");
    } else {
      Retarget(next, "removed");
    }
  }
}

void PipeWireBridge::OnMetadata(std::uint32_t subject, const char* key, const char* type, const char* value) {
  if (subject != PW_ID_CORE) {
    // A stream's target ("pin"): set by WirePlumber's restored stream state, by
    // the sound settings when the user moves an app, by EasyEffects, or by Sukut.
    if (key == nullptr) {
      pins_.erase(subject);
    } else if (std::strcmp(key, kTargetObject) == 0 || std::strcmp(key, kTargetNode) == 0) {
      auto& pins = pins_[subject];
      if (value == nullptr) pins.erase(key);
      else pins[key] = Pin{type ? type : "", value};
      if (pins.empty()) pins_.erase(subject);
    } else {
      return;
    }
    ConsiderPinned(subject);
    return;
  }
  if (key == nullptr) { configured_sink_.clear(); effective_sink_.clear(); return; }
  if (std::strcmp(key, kEffectiveSink) == 0) { effective_sink_ = NameFromJson(value); return; }
  if (std::strcmp(key, kConfiguredSink) != 0) return;
  configured_sink_ = NameFromJson(value);
  if (capture_ == nullptr || configured_sink_.empty() || configured_sink_ == kSinkName) return;
  // The user picked an output in the sound settings while Sukut runs: it is the
  // newest wanted output. Play there, keep Sukut in front of it (even when it is
  // the output Sukut already plays on), and make it the output restored on Off.
  if (switched_default_) {
    blocked_.erase(configured_sink_);  // an explicit choice tries a looping output again
    Want(configured_sink_);
    if (IsOutput(configured_sink_)) Retarget(configured_sink_, "default");
    UpdateRestoreTarget();
    SetConfiguredSink(kSinkName);
  } else if (source_pid_ != 0 && IsOutput(configured_sink_)) {
    Want(configured_sink_);
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

const PipeWireBridge::Node* PipeWireBridge::Find(const std::string& name) const {
  for (const auto& [id, node] : nodes_)
    if (node.name == name) return &node;
  return nullptr;
}

// The last field of an event line: the output's name for people.
std::string PipeWireBridge::Describe(const std::string& name) const {
  const Node* node = Find(name);
  return " description=" + (node != nullptr && !node->description.empty() ? node->description : name);
}

std::uint32_t PipeWireBridge::ProcessOf(const Node& node) const {
  const auto found = clients_.find(node.client);
  return found == clients_.end() ? 0 : found->second;
}

bool PipeWireBridge::IsSourceStream(const Node& node) const {
  return source_pid_ != 0 && node.media_class == "Stream/Output/Audio" && ProcessOf(node) == source_pid_;
}

// An app's playback stream: not Sukut's own, not EasyEffects', and not the
// playing half of a filter or loopback (whose client also owns a sink).
bool PipeWireBridge::IsAppStream(std::uint32_t, const Node& node) const {
  if (node.media_class != "Stream/Output/Audio" || node.name == kPlaybackName) return false;
  if (ProcessOf(node) == static_cast<std::uint32_t>(::getpid()) || OwnsSink(node.client)) return false;
  return Lower(node.app).find("easyeffects") == std::string::npos && Lower(node.app) != "easy effects";
}

bool PipeWireBridge::OwnsSink(std::uint32_t client) const {
  if (client == 0) return false;
  return std::any_of(nodes_.begin(), nodes_.end(), [client](const auto& n) { return n.second.client == client && n.second.media_class == "Audio/Sink"; });
}

std::uint64_t PipeWireBridge::SinkSerial() const {
  const Node* sink = Find(kSinkName);
  return sink != nullptr ? sink->serial : 0;
}

std::string PipeWireBridge::ResolveOutput(const std::string& requested) const {
  if (IsOutput(requested)) return requested;
  if (!requested.empty()) return {};  // an explicit choice that doesn't exist is an error
  if (IsOutput(effective_sink_)) return effective_sink_;
  if (IsOutput(configured_sink_)) return configured_sink_;
  return NextOutput({});
}

// The device WirePlumber would pick: its current default if real, else the
// highest session priority. Never an output that played back into Sukut.
std::string PipeWireBridge::NextOutput(const std::string& excluded) const {
  if (effective_sink_ != excluded && IsOutput(effective_sink_) && blocked_.count(effective_sink_) == 0) return effective_sink_;
  const Node* best = nullptr;
  for (const auto& [id, node] : nodes_)
    if (node.name != excluded && IsOutput(node.name) && blocked_.count(node.name) == 0 && (best == nullptr || node.priority > best->priority))
      best = &node;
  return best ? best->name : std::string();
}

void PipeWireBridge::SetConfiguredSink(const std::string& name) {
  if (name.empty()) pw_metadata_set_property(metadata_, PW_ID_CORE, kConfiguredSink, nullptr, nullptr);
  else pw_metadata_set_property(metadata_, PW_ID_CORE, kConfiguredSink, "Spa:String:JSON", NameToJson(name).c_str());
}

void PipeWireBridge::TargetStream(std::uint32_t id) {
  if (std::find(targets_.begin(), targets_.end(), id) != targets_.end()) return;
  const std::uint64_t serial = SinkSerial();
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
  Event(std::string("OUTPUT_CHANGED to=") + output_ + " reason=" + reason + Describe(output_));
  UpdateRestoreTarget();
}

bool PipeWireBridge::CreateStream(bool playback, const std::string& target, std::string& error) {
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, playback ? "Playback" : "Capture",
      PW_KEY_NODE_NAME, playback ? kPlaybackName : "sukut_capture",
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

// ----- outputs the user wanted (Windows 0.4.2) ----------------------------------

void PipeWireBridge::Want(const std::string& name) {
  if (name.empty() || name == kSinkName) return;
  wanted_.erase(std::remove(wanted_.begin(), wanted_.end(), name), wanted_.end());
  wanted_.push_back(name);
  if (wanted_.size() > kWantedKept) wanted_.erase(wanted_.begin());
}

std::string PipeWireBridge::BestWanted(const std::string& excluded) const {
  for (auto it = wanted_.rbegin(); it != wanted_.rend(); ++it)
    if (*it != excluded && IsOutput(*it) && blocked_.count(*it) == 0) return *it;
  return {};
}

// All system audio: Off restores the default to the most recent wanted output
// that is here (else the most recent one, which WirePlumber uses when it returns).
void PipeWireBridge::UpdateRestoreTarget() {
  if (!switched_default_ || wanted_.empty()) return;
  std::string target = BestWanted();
  if (target.empty()) target = wanted_.back();
  if (target != journal_.previous_sink) {
    journal_.previous_sink = target;
    SaveJournal();
  }
}

bool PipeWireBridge::RecentlyRemoved(const std::string& name) {
  const auto found = removed_at_.find(name);
  return found != removed_at_.end() && Clock::now() - found->second < kRecreatedWithin;
}

// A device connected while Sukut runs: its device object appeared since Start
// (Bluetooth connecting, USB plugged in). A new output of a device that was
// already there is a profile or port change (e.g. in the sound settings), not a
// connection. Outputs without a device object fall back to their name.
bool PipeWireBridge::NewlyConnected(const Node& node) {
  if (!IsDevice(node) || IsDisplay(node)) return false;
  const auto device = devices_.find(node.device);
  return device != devices_.end() ? device->second : !RecentlyRemoved(node.name);
}

void PipeWireBridge::OnOutputAppeared(const Node& node) {
  if (blocked_.count(node.name) != 0) return;
  const bool connected = NewlyConnected(node) && std::find(wanted_.begin(), wanted_.end(), node.name) == wanted_.end();
  // An output the user wanted came back and is newer than where Sukut plays now:
  // the headphones reconnected, or EasyEffects started after Sukut.
  if (BestWanted() == node.name && node.name != output_) {
    Retarget(node.name, "returned");
  } else if (connected) {
    // A newly connected physical output (Bluetooth, USB): use it at once, as
    // Windows does, and make it the output restored on Off.
    Want(node.name);
    Retarget(node.name, "connected");
  } else if (output_.empty()) {
    Retarget(node.name, "appeared");  // the first output after none was left
  }
}

// ----- effects apps (EasyEffects) ------------------------------------------------

// Does Sukut's playback reach its own sink again? Then something (EasyEffects
// following the default output, a filter or loopback) plays Sukut's output back
// into Sukut: a feedback loop. Links carry audio from node to node. Inside an
// effects app or a loopback the audio passes between its own nodes without a
// link, so a virtual sink on the way also leads to every node of its client
// (EasyEffects' sink and its effect nodes; a loopback's sink and its playback).
void PipeWireBridge::CheckLoop() {
  if (capture_ == nullptr || playback_ == nullptr || output_.empty()) return;
  // The current playback's own node: just after a retarget the old one and its
  // links are still listed for a moment.
  const std::uint32_t from = pw_stream_get_node_id(playback_);
  std::uint32_t to = 0, own_client = 0;
  for (const auto& [id, node] : nodes_)
    if (node.name == kSinkName) { to = id; own_client = node.client; }
  if (from == SPA_ID_INVALID || from == 0 || to == 0) return;
  std::set<std::uint32_t> seen{from};
  std::vector<std::uint32_t> queue{from};
  bool loop = false;
  while (!queue.empty() && !loop) {
    const std::uint32_t at = queue.back();
    queue.pop_back();
    const auto visit = [&](std::uint32_t next) {
      if (next == to) loop = true;
      else if (seen.insert(next).second) queue.push_back(next);
    };
    for (const auto& [link, ends] : links_)
      if (ends.first == at) visit(ends.second);
    const auto node = nodes_.find(at);
    if (node != nodes_.end() && IsVirtualSink(node->second) && node->second.client != 0 && node->second.client != own_client)
      for (const auto& [id, other] : nodes_)
        if (id != at && other.client == node->second.client) visit(id);
  }
  if (!loop) return;
  const std::string through = output_;
  blocked_.insert(through);
  Event("FEEDBACK_LOOP through=" + through + Describe(through));
  std::string next = BestWanted(through);
  if (next.empty()) next = NextOutput(through);
  if (next.empty()) {
    if (playback_ != nullptr) { pw_stream_destroy(playback_); playback_ = nullptr; }
    playback_ready_ = false;
    output_.clear();
    Event("OUTPUT_LOST");
  } else {
    Retarget(next, "loop");
  }
}

const PipeWireBridge::Pin* PipeWireBridge::EffectivePin(std::uint32_t id, std::string* key) const {
  const auto found = pins_.find(id);
  if (found == pins_.end()) return nullptr;
  // WirePlumber honours target.object before target.node.
  for (const char* k : {kTargetObject, kTargetNode}) {
    const auto pin = found->second.find(k);
    if (pin != found->second.end()) {
      if (key != nullptr) *key = k;
      return &pin->second;
    }
  }
  return nullptr;
}

// target.object holds an object.serial (or a node.name); target.node a node id.
std::string PipeWireBridge::PinTargetName(const std::string& key, const Pin& pin) const {
  char* end = nullptr;
  const std::uint64_t number = std::strtoull(pin.value.c_str(), &end, 10);
  const bool numeric = !pin.value.empty() && end != nullptr && *end == '\0';
  for (const auto& [id, node] : nodes_) {
    if (!numeric && node.name == pin.value) return node.name;
    if (numeric && (key == kTargetObject ? node.serial == number : id == number)) return node.name;
  }
  return pin.value;  // a target that is not there now
}

// All system audio goes through Sukut's sink by being the default output. A
// stream pinned elsewhere would skip it, unfiltered, while Sukut says nothing is
// playing: EasyEffects' "Process all output streams" pins every app to its own
// sink, and WirePlumber remembers those pins after it is turned off. Sukut points
// such a stream at its sink too and puts the pin back on Off. If someone pins it
// again (EasyEffects still processing all streams, or the user moving it in the
// sound settings), that choice stays and Sukut reports it once.
void PipeWireBridge::ConsiderPinned(std::uint32_t id) {
  if (!routed_ || !switched_default_) return;
  const auto node = nodes_.find(id);
  if (node == nodes_.end() || !IsAppStream(id, node->second)) return;
  std::string key;
  const Pin* pin = EffectivePin(id, &key);
  if (pin != nullptr && PinTargetName(key, *pin) == kSinkName) return;  // through Sukut (also Sukut's own change)
  if (const auto redirected = redirected_.find(id); redirected != redirected_.end()) {
    const std::string app = redirected->second.app;
    redirected_.erase(redirected);
    journal_.pins.erase(std::remove_if(journal_.pins.begin(), journal_.pins.end(), [id](const auto& p) { return p.node == id; }),
                        journal_.pins.end());
    SaveJournal();
    if (pin == nullptr || pin->value == "-1") return;  // unpinned: it follows the default, which is Sukut
    released_.insert(id);
    const std::string target = PinTargetName(key, *pin);
    Event(target == kEasyEffectsSink ? "EASYEFFECTS_PROCESS_ALL app=" + app : "STREAM_PINNED target=" + target + " app=" + app);
    return;
  }
  if (pin == nullptr || pin->value == "-1" || released_.count(id) != 0) return;
  const std::uint64_t serial = SinkSerial();
  if (serial == 0) return;
  PinnedStream saved{id, node->second.app, key, pin->type, pin->value};
  journal_.pins.push_back(saved);
  SaveJournal();  // before the change, so a crash can always undo it
  redirected_[id] = saved;
  pw_metadata_set_property(metadata_, id, kTargetObject, "Spa:Id", std::to_string(serial).c_str());
  Event("STREAM_REDIRECTED node=" + std::to_string(id) + " app=" + saved.app);
}

// A target.object pin is put back as it was; a target.node pin was never
// changed, so only Sukut's target.object is cleared.
void PipeWireBridge::RestorePin(const PinnedStream& pin) {
  if (pin.key == kTargetObject)
    pw_metadata_set_property(metadata_, pin.node, kTargetObject, pin.type.empty() ? nullptr : pin.type.c_str(), pin.value.c_str());
  else
    pw_metadata_set_property(metadata_, pin.node, kTargetObject, nullptr, nullptr);
}

// ----- source app -----------------------------------------------------------------

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
    // Off gives the default to the output Sukut plays for (Windows: Stop restores
    // the selected output), or to the user's own default when none was chosen.
    if (!wanted_.empty()) {
      const std::string best = BestWanted();
      journal_.previous_sink = best.empty() ? wanted_.back() : best;
    }
    if (!WriteJournal(journal_, error)) return false;
    routed_ = true;
    switched_default_ = true;
    SetConfiguredSink(kSinkName);
    std::vector<std::uint32_t> streams;
    for (const auto& [id, node] : nodes_) streams.push_back(id);
    for (const auto id : streams) ConsiderPinned(id);
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
  for (const auto& [id, pin] : redirected_)
    if (nodes_.count(id) != 0) RestorePin(pin);
  redirected_.clear();
  released_.clear();
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
  for (const auto& pin : journal.pins) {
    const auto found = nodes_.find(pin.node);
    if (found == nodes_.end() || found->second.media_class != "Stream/Output/Audio" || found->second.app != pin.app) continue;
    // Put the pin back while the stream still points at Sukut's (now gone) sink;
    // a stream pinned to a real output or unpinned meanwhile is left as it is.
    std::string key;
    const Pin* now = EffectivePin(pin.node, &key);
    if (now == nullptr) continue;
    const std::string target = PinTargetName(key, *now);
    if (target == kSinkName || !IsOutput(target)) RestorePin(pin);
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
  bool missing = false;
  if (output_.empty() && options.fallback_output && !options.output.empty()) {
    output_ = ResolveOutput({});
    missing = !output_.empty();
  }
  if (output_.empty()) {
    error = options.output.empty() ? "no audio output found" : "audio output \"" + options.output + "\" not found";
    return false;
  }
  wanted_.clear();
  blocked_.clear();
  redirected_.clear();
  released_.clear();
  if (!options.output.empty()) wanted_.push_back(options.output);

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
  if (missing) Event("OUTPUT_CHANGED to=" + output_ + " reason=missing" + Describe(output_));
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
    if (bridge.configured_sink_ != kSinkName) catalog.configured_output = bridge.configured_sink_;
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
    if (!journal->pins.empty()) message += "; pins put back: " + std::to_string(journal->pins.size());
    RemoveJournal();
  }
  bridge.Disconnect();
  return true;
}

}  // namespace sukut::linux_audio
