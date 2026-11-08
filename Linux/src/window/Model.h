#pragma once

// What the window shows, as read from the host (HostMain.cpp "read" and "apps").
#include <cstdint>
#include <string>
#include <vector>

#include "window/Json.h"

namespace sukut::window {

// English or Arabic text; every user-visible string goes through this.
inline const char* T(bool arabic, const char* english, const char* arabic_text) { return arabic ? arabic_text : english; }

struct HostState {
  // The engine.
  int mode = 0;   // running mode, 0 = off
  int state = 0;  // 0 off, 1 running, 2 attention
  bool busy = false, standby = false, reconnecting = false, priority_set = false, tray_available = false;
  std::string output, attention;
  std::string notice;  // a problem filtering survives: "feedback_loop:<output>", "easyeffects_process_all:<app>", "stream_pinned:<app>"
  std::uint32_t active_source_pid = 0;
  int active_buffering = -1;
  std::uint64_t underruns = 0, fallbacks = 0, revision = 0;
  // Settings (the host is their only writer).
  std::string language = "en", theme = "system", chosen_output;
  std::uint32_t source_pid = 0;
  int preferred_mode = 1, buffering = 1, amplification_db = 0, background_db = 0;
  bool setup_completed = true, close_to_tray = true, start_with_system = false;
  std::uint64_t settings_version = 0;

  [[nodiscard]] bool Arabic() const { return language == "ar"; }

  static HostState From(const Json& j) {
    HostState s;
    s.mode = static_cast<int>(j.Int("mode"));
    s.state = static_cast<int>(j.Int("state"));
    s.busy = j.Bool("busy");
    s.standby = j.Bool("standby");
    s.reconnecting = j.Bool("reconnecting");
    s.priority_set = j.Bool("prioritySet");
    s.tray_available = j.Bool("trayAvailable");
    s.output = j.Str("output");
    s.attention = j.Str("attention");
    s.notice = j.Str("notice");
    s.active_source_pid = static_cast<std::uint32_t>(j.Int("activeSourcePid"));
    s.active_buffering = static_cast<int>(j.Int("activeBuffering"));
    s.underruns = static_cast<std::uint64_t>(j.Int("underruns"));
    s.fallbacks = static_cast<std::uint64_t>(j.Int("fallbacks"));
    s.revision = static_cast<std::uint64_t>(j.Int("revision"));
    s.language = j.Str("language") == "ar" ? "ar" : "en";
    s.theme = j.Str("theme").empty() ? "system" : j.Str("theme");
    s.chosen_output = j.Str("chosenOutput");
    s.source_pid = static_cast<std::uint32_t>(j.Int("sourcePid"));
    s.preferred_mode = static_cast<int>(j.Int("preferredMode"));
    if (s.preferred_mode < 1 || s.preferred_mode > 3) s.preferred_mode = 1;
    s.buffering = static_cast<int>(j.Int("buffering"));
    s.amplification_db = static_cast<int>(j.Int("amplificationDb"));
    s.background_db = static_cast<int>(j.Int("optimizedBackgroundDb"));
    s.setup_completed = j.Bool("setupCompleted");
    s.close_to_tray = j.Bool("closeToTray");
    s.start_with_system = j.Bool("startWithSystem");
    s.settings_version = static_cast<std::uint64_t>(j.Int("settingsVersion"));
    return s;
  }
};

struct Catalog {
  struct Output { std::string name, description; };
  struct App { std::uint32_t pid = 0; std::string name; };
  std::vector<Output> outputs;
  std::vector<App> apps;
  std::string default_output;
  bool read = false;  // PipeWire answered

  static Catalog From(const Json& j) {
    Catalog c;
    c.read = true;
    for (const auto& o : j["outputs"].items) c.outputs.push_back({o.Str("name"), o.Str("description")});
    for (const auto& a : j["apps"].items) c.apps.push_back({static_cast<std::uint32_t>(a.Int("pid")), a.Str("name")});
    c.default_output = j.Str("defaultOutput");
    return c;
  }
  bool operator==(const Catalog& other) const {
    if (outputs.size() != other.outputs.size() || apps.size() != other.apps.size() || default_output != other.default_output) return false;
    for (std::size_t i = 0; i < outputs.size(); ++i)
      if (outputs[i].name != other.outputs[i].name || outputs[i].description != other.outputs[i].description) return false;
    for (std::size_t i = 0; i < apps.size(); ++i)
      if (apps[i].pid != other.apps[i].pid || apps[i].name != other.apps[i].name) return false;
    return true;
  }
};

}  // namespace sukut::window
