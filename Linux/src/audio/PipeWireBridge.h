#pragma once

// Sukut's audio path on PipeWire (Linux/PLAN.md section 5):
//   apps -> sukut_sink (virtual sink) -> monitor capture -> engine -> playback -> real output
// Two rings join PipeWire's real-time thread to the engine's workers. In
// pass-through mode (stage 2 tests) playback reads the capture ring directly.
//
// Routing (section 5.2): all system audio makes sukut_sink the default output;
// one app points only that app's streams at it. Sukut follows an output the
// user picks while it runs, and the next device if its output is removed.
//
// Outputs, as Windows 0.4.2 does them: Sukut keeps the outputs the user wanted
// this session (the one it started on, one picked in the sound settings, a newly
// connected device), most recent last. A newly connected physical output
// (Bluetooth, USB: its device object appeared while Sukut runs; not HDMI or
// DisplayPort, and not a profile change of a device already there) is used at once. When the output is
// removed Sukut plays on the most recent wanted output still there, and it goes
// back when a more recent one returns. Off gives the default output back to that
// output, or to the newest choice the user made meanwhile.
//
// Effects apps such as EasyEffects (section 5.6):
// - A feedback loop (Sukut's playback reaching sukut_sink again, e.g. EasyEffects
//   following the default output while Sukut plays into it) is broken at once:
//   Sukut moves to another output and reports FEEDBACK_LOOP.
// - For all system audio, app streams pinned to another output in the default
//   metadata (EasyEffects' "Process all output streams", a move in the sound
//   settings) go through Sukut too; their pins are journalled and put back on
//   Off. A stream pinned again by someone else is left alone and reported.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "audio/FrameRing.h"
#include "audio/RoutingJournal.h"

struct spa_dict;
struct spa_pod;
struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_metadata;
struct pw_node;
struct pw_proxy;
struct pw_stream;

namespace sukut::linux_audio {

inline constexpr const char* kSinkName = "sukut_sink";
inline constexpr const char* kEasyEffectsSink = "easyeffects_sink";
inline constexpr std::uint32_t kRate = 48000, kChannels = 2, kHopFrames = 960;
inline constexpr std::uint64_t kNoGap = std::numeric_limits<std::uint64_t>::max();

struct BridgeStats {
  std::uint64_t captured_frames = 0, played_frames = 0, underruns = 0, overruns = 0;
  std::uint32_t fill_frames = 0;
  bool playing = false;
  float volume = 1.0F;
  bool muted = false;
};

class PipeWireBridge {
 public:
  struct Options {
    std::string output;                              // node.name of the real output; empty = current default
    std::uint32_t source_pid = 0;                    // one app (process id); 0 = all system audio
    bool route = true;                               // route audio through Sukut at start
    bool engine = false;                             // capture -> engine -> playback; false = pass-through
    // A chosen output that is missing (headphones off, EasyEffects not running
    // yet): play on the default and go back to it when it appears. Without
    // this, a missing output is an error.
    bool fallback_output = false;
    std::uint32_t capture_frames = 16384;            // capture ring capacity
    std::uint32_t playback_frames = 16384;           // playback ring capacity (engine mode)
    std::uint32_t prebuffer_frames = 2 * kHopFrames; // buffered before playback starts
  };

  PipeWireBridge();
  ~PipeWireBridge();
  PipeWireBridge(const PipeWireBridge&) = delete;
  PipeWireBridge& operator=(const PipeWireBridge&) = delete;

  bool Start(const Options& options, std::string& error);
  // Send the audio through Sukut: the default output, or the chosen app's streams.
  bool Route(std::string& error);
  // Give the audio back (previous default, app streams released), keeping Sukut's nodes for a clean stop.
  void RestoreRouting();
  void Stop();
  [[nodiscard]] BridgeStats Stats() const;
  [[nodiscard]] std::string Output() const;
  // Routing events since the last call, one line each:
  //   OUTPUT_CHANGED to=<node> reason=<reason> description=<text to the end of the line>
  //     reason: default (picked in the sound settings), connected (a new device),
  //     returned (a wanted output came back), removed (the output went away),
  //     appeared (the first output after none), missing (the chosen output is
  //     not there at start), loop (moved away from a feedback loop)
  //   OUTPUT_LOST, SOURCE_TARGETED node=<id>, SOURCE_CLOSED, JOURNAL_FAILED <reason>
  //   FEEDBACK_LOOP through=<node> description=<text>
  //   STREAM_REDIRECTED node=<id> app=<name>         a pinned stream now goes through Sukut
  //   STREAM_PINNED target=<node> app=<name>         pinned again by someone else; left alone
  //   EASYEFFECTS_PROCESS_ALL app=<name>             the same, by EasyEffects
  std::vector<std::string> TakeEvents();
  // Call about once a second: reports SOURCE_CLOSED once the chosen app has exited.
  void CheckSource();

  // Engine side of the rings. The capture ring records where an overrun
  // dropped frames, so the engine can start a fresh segment there.
  FrameRing& CaptureRing() { return capture_ring_; }
  FrameRing& PlaybackRing() { return playback_ring_; }
  [[nodiscard]] std::uint64_t GapAt() const { return gap_at_.load(std::memory_order_acquire); }
  void ClearGap(std::uint64_t at) { gap_at_.compare_exchange_strong(at, kNoGap); }

  // Undoes routing left by a session that did not stop cleanly: the default
  // output (only if it still names Sukut's sink), app streams still aimed at it
  // and the pins of redirected streams.
  static bool RestoreFromJournal(std::string& message);

  // Called from PipeWire's threads; public only for the C callback tables.
  // What the tray menu and the window offer: real outputs and playing apps.
  struct Catalog {
    struct Output { std::string name, description; };
    struct App { std::uint32_t pid = 0; std::string name; };
    std::vector<Output> outputs;
    std::vector<App> apps;
    std::string default_output;     // the real device WirePlumber would use now
    std::string configured_output;  // the user's choice in the sound settings, even while it is missing; "" = none
  };
  // Reads the catalog over a short-lived connection; works whether or not Sukut runs.
  static bool ReadCatalog(Catalog& catalog, std::string& error);

  struct Node {
    std::string name, media_class, description, app;
    std::uint64_t serial = 0;
    std::uint32_t client = 0;  // the app's process id lives on its client object
    int priority = 0;
    // device.id: set on outputs of a physical device (ALSA, Bluetooth), 0 on virtual
    // sinks such as EasyEffects'. The registry shares only a few node properties
    // (not device.api or node.link-group); device.id and client.id are among them.
    std::uint32_t device = 0;
  };
  void OnGlobal(std::uint32_t id, const char* type, const spa_dict* props);
  void OnGlobalRemove(std::uint32_t id);
  void OnMetadata(std::uint32_t subject, const char* key, const char* type, const char* value);
  void OnCoreDone(std::uint32_t id, int seq);
  void OnCoreError(std::uint32_t id, int res, const char* message);
  void OnSinkProps(const spa_pod* param);
  void OnCaptureProcess();
  void OnPlaybackProcess();
  void OnStreamState(bool playback, int state, const char* error);

 private:
  using Clock = std::chrono::steady_clock;
  struct Pin { std::string type, value; };

  bool Connect(std::string& error);
  void Disconnect();
  bool Roundtrip();
  bool WaitForNode(const std::string& name, int attempts);
  [[nodiscard]] bool IsOutput(const std::string& name) const;
  [[nodiscard]] const Node* Find(const std::string& name) const;
  [[nodiscard]] std::string Describe(const std::string& name) const;
  [[nodiscard]] std::string ResolveOutput(const std::string& requested) const;
  [[nodiscard]] std::string NextOutput(const std::string& excluded) const;
  [[nodiscard]] bool IsSourceStream(const Node& node) const;
  [[nodiscard]] bool IsAppStream(std::uint32_t id, const Node& node) const;
  [[nodiscard]] bool OwnsSink(std::uint32_t client) const;
  [[nodiscard]] std::uint32_t ProcessOf(const Node& node) const;
  [[nodiscard]] std::uint64_t SinkSerial() const;
  bool CreateStream(bool playback, const std::string& target, std::string& error);
  void SetConfiguredSink(const std::string& name);
  bool RouteLocked(std::string& error);
  void RestoreLocked();
  void UndoJournalLocked(const RoutingJournal& journal);
  void TargetStream(std::uint32_t id);
  void Retarget(const std::string& output, const char* reason);
  void ApplyVolume();
  void NoteSourceGone();
  void SaveJournal();
  void Event(std::string text);

  // Outputs (loop thread, lock held).
  void Want(const std::string& name);
  [[nodiscard]] std::string BestWanted(const std::string& excluded = {}) const;
  void UpdateRestoreTarget();
  void OnOutputAppeared(const Node& node);
  [[nodiscard]] bool RecentlyRemoved(const std::string& name);
  [[nodiscard]] bool NewlyConnected(const Node& node);
  // Effects apps (loop thread, lock held).
  void CheckLoop();
  void ConsiderPinned(std::uint32_t id);
  void RestorePin(const PinnedStream& pin);
  [[nodiscard]] const Pin* EffectivePin(std::uint32_t id, std::string* key = nullptr) const;
  [[nodiscard]] std::string PinTargetName(const std::string& key, const Pin& pin) const;

  pw_thread_loop* loop_ = nullptr;
  pw_context* context_ = nullptr;
  pw_core* core_ = nullptr;
  pw_registry* registry_ = nullptr;
  pw_metadata* metadata_ = nullptr;
  pw_proxy* sink_proxy_ = nullptr;
  pw_node* sink_node_ = nullptr;
  pw_stream* capture_ = nullptr;
  pw_stream* playback_ = nullptr;
  struct Hooks;
  std::unique_ptr<Hooks> hooks_;

  // Loop-thread state (read elsewhere only under the loop lock).
  std::map<std::uint32_t, Node> nodes_;
  std::map<std::uint32_t, std::uint32_t> clients_;  // client id -> process id
  std::string configured_sink_, effective_sink_, output_, stream_error_;
  int pending_seq_ = 0;
  bool done_ = false, routed_ = false, switched_default_ = false, playback_ready_ = false, source_closed_ = false;
  std::uint32_t source_pid_ = 0;
  std::vector<std::uint32_t> targets_;
  RoutingJournal journal_;
  std::vector<float> volumes_;
  bool have_mute_ = false, mute_ = false;

  std::vector<std::string> wanted_;                         // outputs the user wanted, most recent last
  std::set<std::string> blocked_;                           // outputs that played back into Sukut
  std::map<std::string, Clock::time_point> removed_at_;     // physical outputs removed lately
  std::map<std::uint32_t, bool> devices_;                   // device objects -> appeared while Sukut runs
  std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> links_;  // link -> (output node, input node)
  std::map<std::uint32_t, std::map<std::string, Pin>> pins_;               // stream -> its target metadata by key
  std::map<std::uint32_t, PinnedStream> redirected_;        // pinned streams Sukut sent through its sink
  std::set<std::uint32_t> released_;                        // pinned again by someone else: left alone

  mutable std::mutex events_mutex_;
  std::vector<std::string> events_;

  FrameRing capture_ring_, playback_ring_;
  bool engine_ = false;
  std::uint32_t prebuffer_ = 0;
  std::atomic<bool> playing_{false};  // written by the playback thread only
  std::atomic<std::uint64_t> gap_at_{kNoGap};

  std::atomic<std::uint64_t> captured_{0}, played_{0}, underruns_{0}, overruns_{0};
  std::atomic<float> volume_{1.0F};
  std::atomic<bool> muted_{false};
};

}  // namespace sukut::linux_audio
