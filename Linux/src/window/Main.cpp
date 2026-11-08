// sukut: Sukut's window on Linux (Linux/PLAN.md section 7).
//   sukut                    open the window (starting sukut-host for the tray if needed)
//   sukut --tray             start Sukut in the tray only (the autostart entry)
//   sukut --restore-audio    undo audio routing left by a crash
//   sukut --capture DIR [--capture-set main|contrast|keyboard|phone|easyeffects]   screenshots and keyboard check, no host
//   sukut --phone devices|inspect|status|install|update|setup|open|activate|deactivate|allow-usb [--serial S]
//                            the Phone page's actions from a terminal (support and tests)
// The window is a single-instance GtkApplication: launching it again raises
// the open window (with the launcher's activation token on Wayland).
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <adwaita.h>

#include <iostream>

#include "window/Capture.h"
#include "window/HostLink.h"
#include "window/PhoneTools.h"
#include "window/Style.h"
#include "window/Window.h"

namespace {

using namespace sukut::window;

// sukut-host beside this program in a build folder, else the installed one.
std::string HostCommand() {
  std::error_code code;
  const auto here = std::filesystem::read_symlink("/proc/self/exe", code).parent_path();
  for (const auto& candidate : {here / "sukut-host", std::filesystem::path("/usr/libexec/sukut/sukut-host")}) {
    if (!std::filesystem::exists(candidate)) continue;
    gchar* quoted = g_shell_quote(candidate.c_str());
    std::string command = quoted;
    g_free(quoted);
    return command;
  }
  return "sukut-host";
}

struct App {
  std::unique_ptr<HostLink> link;
  std::unique_ptr<SukutWindow> window;
};

// The Phone page's actions without the window: the same PhoneTools calls.
int RunPhone(const std::string& command, const std::string& serial) {
  PhoneTools tools;
  try {
    const auto devices = tools.Devices();
    if (command == "devices") {
      for (const auto& d : devices) std::cout << d.serial << " " << d.state << " " << d.model << "\n";
      return 0;
    }
    if (command == "allow-usb") {
      std::cout << "OK: " << tools.AllowUsbAccess() << std::endl;
      return 0;
    }
    const auto device = PhoneTools::Select(devices, serial);
    if (!device) throw PhoneError(devices.empty() ? "Connect your phone by USB and allow debugging." : "Select your phone first.");
    if (device->state != "device") throw PhoneError(PhoneTools::NotReady(*device));
    std::string result;
    if (command == "inspect") {
      const auto info = tools.Inspect(*device);
      result = info.name + " · Android " + info.android + " · Sukut " +
               (info.sukut_code ? std::to_string(*info.sukut_code) : std::string("not installed")) + (info.worker_ready ? " · worker ready" : " · activation needed");
    } else if (command == "status") {
      const auto status = tools.Status(*device);
      result = "installed=" + (status.installed_code ? std::to_string(*status.installed_code) : std::string("none")) +
               " bundled=" + std::to_string(status.bundled_code) + " (" + status.bundled_version + ")";
    } else if (command == "install") result = tools.Install(*device, false);
    else if (command == "update") result = tools.Install(*device, true);
    else if (command == "setup") result = tools.Setup(*device);
    else if (command == "open") result = tools.Open(*device);
    else if (command == "activate") result = tools.Activate(*device);
    else if (command == "deactivate") result = tools.Deactivate(*device);
    else { std::cerr << "unknown phone command: " << command << "\n"; return 2; }
    std::cout << "OK: " << result << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cout << "ERROR: " << error.what() << std::endl;
    return 1;
  }
}

void Activate(GApplication* application, gpointer data) {
  auto* app = static_cast<App*>(data);
  if (app->window) { app->window->Present(); return; }
  InstallStyle();
  app->link = std::make_unique<HostLink>();
  std::string error;
  if (!app->link->Connect(HostCommand(), error)) {
    // Nothing else can work without the host: say so in a plain window.
    GtkWidget* window = adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(window), "سُكوت · Sukut");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);
    GtkWidget* view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
    GtkWidget* page = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(page), "dialog-warning-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(page), "Sukut could not start");
    adw_status_page_set_description(ADW_STATUS_PAGE(page), error.c_str());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);
    gtk_window_present(GTK_WINDOW(window));
    return;
  }
  app->window = std::make_unique<SukutWindow>(ADW_APPLICATION(application), app->link.get());
  // The host went away (Exit from the tray, logout, crash): the window goes too.
  app->link->OnLost([application] { g_application_quit(application); });
  app->window->Present();
  app->window->Start();
}

}  // namespace

int main(int argc, char** argv) {
  std::string capture, capture_set = "main", phone, serial;
  bool tray = false, restore = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--tray") tray = true;
    else if (arg == "--restore-audio") restore = true;
    else if (arg == "--capture" && i + 1 < argc) capture = argv[++i];
    else if (arg == "--capture-set" && i + 1 < argc) capture_set = argv[++i];
    else if (arg == "--phone" && i + 1 < argc) phone = argv[++i];
    else if (arg == "--serial" && i + 1 < argc) serial = argv[++i];
  }
  if (!phone.empty()) return RunPhone(phone, serial);
  if (restore) {
    gint status = 1;
    const std::string command = HostCommand() + " --restore-audio";
    g_spawn_command_line_sync(command.c_str(), nullptr, nullptr, &status, nullptr);
    return g_spawn_check_wait_status(status, nullptr) ? 0 : 1;
  }
  if (tray) {
    // The host is the tray; it ignores a second --tray launch by itself.
    const std::string command = HostCommand() + " --tray";
    return g_spawn_command_line_async(command.c_str(), nullptr) ? 0 : 1;
  }

  LoadFonts();
  if (!capture.empty()) {
    AdwApplication* app = adw_application_new("io.github.altarafiya.Sukut.Capture", G_APPLICATION_NON_UNIQUE);
    const int status = RunCapture(app, capture, capture_set);
    g_object_unref(app);
    return status;
  }
  AdwApplication* app = adw_application_new("io.github.altarafiya.Sukut", G_APPLICATION_DEFAULT_FLAGS);
  App state;
  g_signal_connect(app, "activate", G_CALLBACK(Activate), &state);
  char* plain[] = {argv[0], nullptr};
  const int status = g_application_run(G_APPLICATION(app), 1, plain);
  state.window.reset();
  state.link.reset();
  g_object_unref(app);
  return status;
}
