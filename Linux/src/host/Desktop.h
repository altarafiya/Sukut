#pragma once

// Desktop services for the host (Linux/PLAN.md section 6): the close-to-tray
// notification, the optional autostart entry and RealtimeKit priority for the
// engine's processing thread.
#include <filesystem>
#include <string>

#include <gio/gio.h>

namespace sukut::host {

// org.freedesktop.Notifications, as app "Sukut" with the app icon. A new notice
// replaces Sukut's previous one. Asynchronous; a missing server is ignored.
void Notify(GDBusConnection* session_bus, const std::string& title, const std::string& body);

// $XDG_CONFIG_HOME/autostart/io.github.altarafiya.Sukut.desktop. Written only
// when the user turns on Start with system; the package never enables it.
std::filesystem::path AutostartPath();
bool SetAutostart(bool enabled, const std::string& command, std::string& error);

// Lets the engine ask RealtimeKit (system bus) for its processing thread's nice
// value when the kernel refuses it. Normal priority remains the fallback.
void UseRealtimeKit();

}  // namespace sukut::host
