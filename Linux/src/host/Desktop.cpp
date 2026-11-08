#include "host/Desktop.h"

#include "audio/Sandbox.h"
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <system_error>

#include "realtime/ProcessingThreadTask.h"

namespace sukut::host {
namespace {

constexpr const char* kAppId = "io.github.altarafiya.Sukut";
guint32 g_last_notice = 0;  // main thread only

// In Flatpak the portal stands in for RealtimeKit (the sandbox has no system bus).
bool PortalHighPriority(long thread_id, int nice) {
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (bus == nullptr) { if (error) g_error_free(error); return false; }
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Realtime",
      "MakeThreadHighPriorityWithPID", g_variant_new("(tti)", static_cast<guint64>(::getpid()), static_cast<guint64>(thread_id), nice),
      nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
  g_object_unref(bus);
  if (reply == nullptr) {
    std::cerr << "sukut-host: the Realtime portal refused nice " << nice << ": " << (error ? error->message : "no reply")
              << "; processing continues at normal priority" << std::endl;
    if (error) g_error_free(error);
    return false;
  }
  g_variant_unref(reply);
  std::cerr << "sukut-host: the Realtime portal set the processing thread to nice " << nice << std::endl;
  return true;
}

bool RealtimeKitHighPriority(long thread_id, int nice) {
  if (InSandbox()) return PortalHighPriority(thread_id, nice);
  // One system-bus connection for the process; the call runs once per engine start.
  static GDBusConnection* const system = [] {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (error) g_error_free(error);
    return bus;
  }();
  if (system == nullptr) {
    std::cerr << "sukut-host: no system bus for RealtimeKit; processing continues at normal priority" << std::endl;
    return false;
  }
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      system, "org.freedesktop.RealtimeKit1", "/org/freedesktop/RealtimeKit1", "org.freedesktop.RealtimeKit1",
      "MakeThreadHighPriority", g_variant_new("(ti)", static_cast<guint64>(thread_id), nice), nullptr,
      G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, &error);
  // Once per engine start, from the processing thread.
  if (reply == nullptr) {
    std::cerr << "sukut-host: RealtimeKit refused nice " << nice << ": " << (error ? error->message : "no reply")
              << "; processing continues at normal priority" << std::endl;
    if (error) g_error_free(error);
    return false;
  }
  g_variant_unref(reply);
  std::cerr << "sukut-host: RealtimeKit set the processing thread to nice " << nice << std::endl;
  return true;
}

}  // namespace

void Notify(GDBusConnection* session_bus, const std::string& title, const std::string& body) {
  if (session_bus == nullptr) return;
  GVariantBuilder hints;
  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(kAppId));
  g_dbus_connection_call(
      session_bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
      "Notify",
      g_variant_new("(susss@asa{sv}i)", "Sukut", g_last_notice, kAppId, title.c_str(), body.c_str(),
                    g_variant_new_strv(nullptr, 0), &hints, -1),
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer) {
        GError* error = nullptr;
        if (GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error)) {
          g_variant_get(reply, "(u)", &g_last_notice);
          g_variant_unref(reply);
        }
        if (error) g_error_free(error);
      },
      nullptr);
}

std::filesystem::path AutostartPath() {
  if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
    return std::filesystem::path(config) / "autostart" / (std::string(kAppId) + ".desktop");
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : ".") / ".config" / "autostart" / (std::string(kAppId) + ".desktop");
}

bool SetAutostart(bool enabled, const std::string& command, std::string& error) {
  const auto path = AutostartPath();
  std::error_code code;
  if (!enabled) {
    std::filesystem::remove(path, code);
    if (code) { error = "cannot remove " + path.string() + ": " + code.message(); return false; }
    return true;
  }
  std::filesystem::create_directories(path.parent_path(), code);
  std::ofstream out(path, std::ios::trunc);
  out << "[Desktop Entry]\n"
         "Type=Application\n"
         "Name=Sukut\n"
         "Name[ar]=سُكوت\n"
         "Comment=Internet without music\n"
         "Comment[ar]=إنترنت بلا موسيقى\n"
         "Exec=" << command << "\n"
         "Icon=" << kAppId << "\n"
         "Terminal=false\n"
         "X-GNOME-Autostart-enabled=true\n";
  if (!out) { error = "cannot write " + path.string(); return false; }
  return true;
}

void UseRealtimeKit() { cleanwave::SetPriorityRequest(RealtimeKitHighPriority); }

}  // namespace sukut::host
