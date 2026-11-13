#pragma once

// Sukut's settings on Linux: $XDG_CONFIG_HOME/sukut/settings.json, with the
// Windows key names (Linux/PLAN.md section 7). Output is a PipeWire node.name.
#include <cstdint>
#include <filesystem>
#include <string>
#include "live/OptimizedMix.h"

namespace sukut::host {

struct HostSettings {
  std::string language = "en";  // "en" or "ar"
  std::string theme = "system";
  std::string output;           // real output node.name; empty = the current default
  // The output chosen in the system's sound settings when `output` was last
  // chosen or Sukut last stopped. If the system's choice differs at the next
  // Start, the user changed it while Sukut was Off, and Sukut follows it
  // (Windows 0.4.2). Empty in settings from before 0.4.0~beta.7.
  std::string output_default;
  std::uint32_t source_pid = 0; // one app; 0 = all system audio
  int preferred_mode = 1;       // the mode Power turns on
  int buffering = 1;
  int amplification_db = 0;
  int optimized_background_db = 0;
  cleanwave::OptimizedMix optimized_mix;
  bool setup_completed = false;
  bool close_to_tray = true;
  bool start_with_system = false;
  std::uint64_t host_settings_version = 0;

  static std::filesystem::path Path();
  static HostSettings Load();
  bool Save(std::string& error) const;
  [[nodiscard]] bool Arabic() const { return language == "ar"; }
};

// Small JSON helpers shared by the host (flat objects only).
std::string JsonEscape(const std::string& text);

}  // namespace sukut::host
