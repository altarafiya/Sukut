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

struct RoutingJournal {
  // All system audio: default.configured.audio.sink to put back (empty = none was set).
  // Updated when the user picks another output while Sukut runs, or the output is removed.
  std::string previous_sink;
  bool default_switched = false;
  // One app: its process and the stream nodes Sukut pointed at its sink.
  std::uint32_t source_pid = 0;
  std::vector<std::uint32_t> targets;
};

std::filesystem::path JournalPath();
bool WriteJournal(const RoutingJournal& journal, std::string& error);
std::optional<RoutingJournal> ReadJournal();
void RemoveJournal();

}  // namespace sukut::linux_audio
