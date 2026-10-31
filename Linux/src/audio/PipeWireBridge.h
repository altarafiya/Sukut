#pragma once

// Sukut's audio path on PipeWire (Linux/PLAN.md section 5):
//   apps -> sukut_sink (virtual sink) -> monitor capture -> engine -> playback -> real output
// Two rings join PipeWire's real-time thread to the engine's workers. In
// pass-through mode (stage 2 tests) playback reads the capture ring directly.
//
// Routing (section 5.2): all system audio makes sukut_sink the default output;
// one app points only that app's streams at it. Sukut follows an output the
// user picks while it runs, and the next device if its output is removed.
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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
  // OUTPUT_CHANGED to=<node> reason=default|removed|appeared, OUTPUT_LOST,
  // SOURCE_TARGETED node=<id>, SOURCE_CLOSED, JOURNAL_FAILED <reason>.
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
  // output (only if it still names Sukut's sink) and app streams still aimed at it.
  static bool RestoreFromJournal(std::string& message);

  // Called from PipeWire's threads; public only for the C callback tables.
  // What the tray menu and the window offer: real outputs and playing apps.
  struct Catalog {
    struct Output { std::string name, description; };
    struct App { std::uint32_t pid = 0; std::string name; };
    std::vector<Output> outputs;
    std::vector<App> apps;
    std::string default_output;  // the real device WirePlumber would use now
  };
  // Reads the catalog over a short-lived connection; works whether or not Sukut runs.
  static bool ReadCatalog(Catalog& catalog, std::string& error);

  struct Node {
    std::string name, media_class, description, app;
    std::uint64_t serial = 0;
    std::uint32_t client = 0;  // the app's process id lives on its client object
    int priority = 0;
  };
  void OnGlobal(std::uint32_t id, const char* type, const spa_dict* props);
  void OnGlobalRemove(std::uint32_t id);
  void OnMetadata(std::uint32_t subject, const char* key, const char* value);
  void OnCoreDone(std::uint32_t id, int seq);
  void OnCoreError(std::uint32_t id, int res, const char* message);
  void OnSinkProps(const spa_pod* param);
  void OnCaptureProcess();
  void OnPlaybackProcess();
  void OnStreamState(bool playback, int state, const char* error);

 private:
  bool Connect(std::string& error);
  void Disconnect();
  bool Roundtrip();
  bool WaitForNode(const std::string& name, int attempts);
  [[nodiscard]] bool IsOutput(const std::string& name) const;
  [[nodiscard]] std::string ResolveOutput(const std::string& requested) const;
  [[nodiscard]] std::string NextOutput(const std::string& excluded) const;
  [[nodiscard]] bool IsSourceStream(const Node& node) const;
  [[nodiscard]] std::uint32_t ProcessOf(const Node& node) const;
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
