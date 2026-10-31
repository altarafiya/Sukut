#pragma once

// Sukut's tray on Linux (Linux/PLAN.md section 6): a StatusNotifierItem with a
// com.canonical.dbusmenu menu that mirrors the Windows flyout. Left click opens
// (or raises) the window. While the window is open the menu offers only Open
// Sukut and Exit, as the Windows tray brings the window forward then.
// Stock GNOME has no StatusNotifierWatcher until the AppIndicator extension is
// enabled: Available() is false then, and the host turns off close to tray.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h>

namespace sukut::host {

class Tray {
 public:
  struct State {
    bool arabic = false, window_open = false, on = false, standby = false, attention = false, busy = false;
    int mode = 0, preferred = 1, buffering = 1;
    std::string status;  // the status line, already in the right language
    std::vector<std::pair<std::string, std::string>> outputs;  // node.name, description
    std::string output;
    std::vector<std::pair<std::uint32_t, std::string>> apps;   // pid, name
    std::uint32_t source_pid = 0;
  };
  struct Actions {
    std::function<void()> activate;                 // left click
    std::function<void(bool)> power;                // the On/Off checkbox
    std::function<void(int)> select_mode;           // 1..3
    std::function<void(int)> set_buffering;         // 0..2
    std::function<void(const std::string&)> set_output;
    std::function<void(std::uint32_t)> set_source;  // 0 = all system audio
    std::function<void()> open, exit;
    std::function<void()> refresh;                  // the menu is about to show: refresh outputs and apps
    std::function<void(const std::string&)> activation_token;
    std::function<void(bool)> availability;         // a tray host appeared or went away
  };

  Tray(Actions actions, std::string icon_theme_path);
  ~Tray();
  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  bool Start(GDBusConnection* connection, std::string& error);
  void Update(const State& state);
  [[nodiscard]] bool Available() const { return registered_; }
  [[nodiscard]] const std::string& BusName() const { return bus_name_; }

  // GDBus entry points (public only for the C vtables).
  void OnItemMethod(const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation);
  GVariant* OnItemProperty(const gchar* property);
  void OnMenuMethod(const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation);
  GVariant* OnMenuProperty(const gchar* property);
  void OnWatcher(bool present);
  void OnRegistered(bool ok);

 private:
  struct Item {
    std::string label, type, toggle, icon;
    int toggle_state = -1;
    bool enabled = true, visible = true;
    std::vector<int> children;
    std::function<void()> action;
  };
  void Build(const State& state);
  GVariant* Layout(int id, int depth) const;
  GVariant* Properties(int id) const;
  void Emit(const char* path, const char* interface, const char* signal, GVariant* parameters);
  [[nodiscard]] std::string IconName() const;

  Actions actions_;
  std::string icon_theme_path_, bus_name_;
  GDBusConnection* connection_ = nullptr;
  guint name_id_ = 0, watch_id_ = 0, item_object_ = 0, menu_object_ = 0;
  GDBusNodeInfo* item_info_ = nullptr;
  GDBusNodeInfo* menu_info_ = nullptr;
  bool registered_ = false;
  State state_;
  std::map<int, Item> items_;
  guint revision_ = 1;
  std::string icon_, status_, tooltip_;
};

}  // namespace sukut::host
