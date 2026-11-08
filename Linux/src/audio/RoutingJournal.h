#pragma once

// What Sukut changed in the user's audio routing, written before each change
// and removed after it is undone (Linux/PLAN.md section 5.5). A journal left
// behind means Sukut stopped without restoring: the next start restores it.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sukut::linux_audio {

// An app stream that was pinned to another output (a "target.object" or
// "target.node" in the default metadata, e.g. by EasyEffects' "Process all output
// streams" or a move in the sound settings) and that Sukut sent through its sink
// for all system audio. The pin is put back on Off.
struct PinnedStream {
  std::uint32_t node = 0;
  std::string app;    // application.name, to check the node id was not reused
  std::string key;    // the pin's key: target.object (restored as it was) or target.node (left as it is)
  std::string type, value;
};

struct RoutingJournal {
  // All system audio: default.configured.audio.sink to put back (empty = none was set).
  // It is the output the user wants: the one Sukut started on, one picked in the
  // sound settings or a newly connected device (Windows 0.4.2).
  std::string previous_sink;
  bool default_switched = false;
  // One app: its process and the stream nodes Sukut pointed at its sink.
  std::uint32_t source_pid = 0;
  std::vector<std::uint32_t> targets;
  std::vector<PinnedStream> pins;  // all system audio: pinned streams Sukut redirected
};

std::filesystem::path JournalPath();
bool WriteJournal(const RoutingJournal& journal, std::string& error);
std::optional<RoutingJournal> ReadJournal();
void RemoveJournal();

}  // namespace sukut::linux_audio
