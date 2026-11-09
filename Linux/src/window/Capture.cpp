#include "window/Capture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "window/Style.h"
#include "window/Widgets.h"
#include "window/Window.h"

namespace sukut::window {
namespace {

struct Scene {
  std::string name;
  int width, height;
  std::function<void(SukutWindow&)> setup;
  std::function<void(SukutWindow&)> after;  // once laid out (scrolling)
};

HostState Base(const std::string& language, const std::string& theme) {
  HostState s;
  s.language = language;
  s.theme = theme;
  s.tray_available = true;
  s.close_to_tray = true;
  s.setup_completed = true;
  s.preferred_mode = 1;
  s.buffering = 1;
  s.background_db = -6;
  s.amplification_db = 3;
  return s;
}

HostState On(HostState s, int mode, bool standby = false) {
  s.mode = mode;
  s.state = 1;
  s.standby = standby;
  s.output = "alsa_output.pci-0000_00_1f.3.analog-stereo";
  s.preferred_mode = mode;
  return s;
}

Catalog Devices() {
  Catalog c;
  c.read = true;
  c.outputs = {{"alsa_output.pci-0000_00_1f.3.analog-stereo", "Speakers (Built-in Audio)"},
               {"bluez_output.AC_80_0A_12_34_56.1", "WH-1000XM4 Headphones"}};
  c.apps = {{4242, "Firefox"}, {4343, "Videos"}};
  c.default_output = "alsa_output.pci-0000_00_1f.3.analog-stereo";
  return c;
}

PhoneDevice Pixel();
PhoneInfo PixelInfo(bool installed, bool ready);

std::function<void(SukutWindow&)> Show(HostState state, std::function<void(SukutWindow&)> then = {}) {
  return [state, then](SukutWindow& w) {
    w.Apply(Devices());
    w.Apply(state);
    if (then) then(w);
  };
}

bool Snapshot(GtkWidget* window, const std::filesystem::path& file) {
  const int width = gtk_widget_get_width(window), height = gtk_widget_get_height(window);
  GdkPaintable* paintable = gtk_widget_paintable_new(window);
  GtkSnapshot* snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, width, height);
  GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
  g_object_unref(paintable);
  if (!node) return false;
  GskRenderer* renderer = gsk_cairo_renderer_new();
  if (!gsk_renderer_realize_for_display(renderer, gdk_display_get_default(), nullptr)) { gsk_render_node_unref(node); return false; }
  const graphene_rect_t viewport = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(width), static_cast<float>(height));
  GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &viewport);
  const bool saved = gdk_texture_save_to_png(texture, file.c_str());
  g_object_unref(texture);
  gsk_renderer_unrealize(renderer);
  g_object_unref(renderer);
  gsk_render_node_unref(node);
  return saved;
}

// What a screen reader would announce for a focus stop.
std::string Describe(GtkWidget* widget) {
  GtkWidget* x = widget;
  for (int depth = 0; x && depth < 6; ++depth, x = gtk_widget_get_parent(x)) {
    if (const char* name = NameOf(x)) return name;
    if (ADW_IS_PREFERENCES_ROW(x)) return adw_preferences_row_get_title(ADW_PREFERENCES_ROW(x));
    if (GTK_IS_BUTTON(x) && gtk_button_get_label(GTK_BUTTON(x))) return gtk_button_get_label(GTK_BUTTON(x));
  }
  std::function<std::string(GtkWidget*)> first_label = [&](GtkWidget* w) -> std::string {
    if (GTK_IS_LABEL(w) && *gtk_label_get_text(GTK_LABEL(w))) return gtk_label_get_text(GTK_LABEL(w));
    for (GtkWidget* c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
      if (auto text = first_label(c); !text.empty()) return text;
    return "";
  };
  return first_label(widget);
}

void DumpFocusables(GtkWidget* widget, int depth = 0) {
  if (gtk_widget_get_focusable(widget) || GTK_IS_LIST_BOX_ROW(widget) || GTK_IS_SCROLLED_WINDOW(widget) || GTK_IS_VIEWPORT(widget) ||
      ADW_IS_PREFERENCES_GROUP(widget) || GTK_IS_LIST_BOX(widget))
    g_printerr("%*s%s '%s' mapped=%d visible=%d childvis=%d can_focus=%d focusable=%d sensitive=%d h=%d\n", depth, "",
               G_OBJECT_TYPE_NAME(widget), Describe(widget).substr(0, 30).c_str(), gtk_widget_get_mapped(widget), gtk_widget_get_visible(widget),
               gtk_widget_get_child_visible(widget), gtk_widget_get_can_focus(widget), gtk_widget_get_focusable(widget),
               gtk_widget_is_sensitive(widget), gtk_widget_get_height(widget));
  for (GtkWidget* c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c)) DumpFocusables(c, depth + 1);
}

// Tab through the window from `start`, as the keyboard would.
std::vector<std::string> FocusWalk(GtkWidget* window, GtkWidget* start) {
  std::vector<std::string> stops;
  std::set<GtkWidget*> visited;  // one full cycle: Tab wraps to the sidebar's current row, not always the first
  gtk_widget_grab_focus(start);
  GtkWidget* first = gtk_root_get_focus(GTK_ROOT(window));
  for (int step = 0; step < 300; ++step) {
    GtkWidget* focus = gtk_root_get_focus(GTK_ROOT(window));
    if (focus && !visited.insert(focus).second) break;
    if (focus) stops.push_back(std::string(G_OBJECT_TYPE_NAME(focus)) + " | " + Describe(focus));
    if (!gtk_widget_child_focus(window, GTK_DIR_TAB_FORWARD)) {
      if (g_getenv("SUKUT_FOCUS_DEBUG")) g_printerr("focus wrapped after %s\n", focus ? Describe(focus).c_str() : "(none)");
      gtk_widget_child_focus(window, GTK_DIR_TAB_FORWARD);
    }
    if (gtk_root_get_focus(GTK_ROOT(window)) == first) break;
  }
  return stops;
}

class Runner {
 public:
  Runner(AdwApplication* app, std::filesystem::path dir, std::vector<Scene> scenes, bool keyboard)
      : app_(app), dir_(std::move(dir)), scenes_(std::move(scenes)), keyboard_(keyboard) {}

  void Next() {
    if (window_) { gtk_window_destroy(window_->Window()); window_.reset(); }
    if (index_ >= scenes_.size()) {
      if (keyboard_) { Keyboard(); return; }
      Finish();
      return;
    }
    const Scene& scene = scenes_[index_];
    window_ = std::make_unique<SukutWindow>(app_, nullptr);
    gtk_window_set_default_size(window_->Window(), scene.width, scene.height);
    window_->Present();
    Later(700, [this] {
      scenes_[index_].setup(*window_);
      // A language change rebuilds the window; give the headless display time to lay it out.
      Later(1200, [this] {
        if (scenes_[index_].after) scenes_[index_].after(*window_);
        Later(700, [this] {
          const auto file = dir_ / (scenes_[index_].name + ".png");
          bool ok = Snapshot(GTK_WIDGET(window_->Window()), file);
          for (int retry = 0; !ok && retry < 3; ++retry) {
            g_usleep(300000);
            while (g_main_context_iteration(nullptr, FALSE)) {}
            ok = Snapshot(GTK_WIDGET(window_->Window()), file);
          }
          results_ << "capture_" << scenes_[index_].name << "=" << (ok ? "PASS" : "FAIL") << " "
                   << gtk_widget_get_width(GTK_WIDGET(window_->Window())) << "x" << gtk_widget_get_height(GTK_WIDGET(window_->Window())) << "\n";
          if (!ok) failed_ = true;
          ++index_;
          Next();
        });
      });
    });
  }

 private:
  void Later(guint ms, std::function<void()> task) {
    auto* boxed = new std::function<void()>(std::move(task));
    g_timeout_add_full(G_PRIORITY_DEFAULT, ms, [](gpointer d) -> gboolean { (*static_cast<std::function<void()>*>(d))(); return G_SOURCE_REMOVE; },
                       boxed, [](gpointer d) { delete static_cast<std::function<void()>*>(d); });
  }

  // Every control must be reachable with Tab and have a name.
  void Keyboard() {
    window_ = std::make_unique<SukutWindow>(app_, nullptr);
    gtk_window_set_default_size(window_->Window(), 1040, 720);
    window_->Present();
    Later(700, [this] {
      Show(On(Base("en", "dark"), 2))(*window_);
      Later(1500, [this] {
        Walk("audio", {"Audio", "Settings", "Turn off Sukut", "Audio modes and buffering help", "Music Filter", "Optimized", "Strict",
                       "Background level", "Speakers (Built-in Audio)", "WH-1000XM4 Headphones", "Refresh audio outputs and source apps",
                       "All system audio", "Firefox", "Videos", "Balanced"});
        window_->ShowPage(SukutWindow::Page::Phone);
        window_->SetPhoneState({Pixel()}, PixelInfo(true, true));
        Later(1500, [this] {
        Walk("phone", {"Audio", "Phone", "Settings", "Full setup", "Activate Sukut", "Install or update Sukut", "USB connection help",
                       "Refresh connected phone"});
        window_->ShowPage(SukutWindow::Page::Settings);
        // The headless display lays out a long page slowly; Tab sorts by position, so wait for it.
        Later(1500, [this] {
          Walk("settings", {"Language", "App theme", "Close to tray", "Start with system", "Audio amplification", "Sound settings",
                            "Open guide", "Refresh devices", "Copy diagnostics", "What’s new"});
          window_->OpenHelp();
          Later(3000, [this] {
            // Tab from the dialog must reach its Done button.
            GtkWidget* dialog = window_->HelpDialog();
            std::string seen;
            bool done = false;
            std::ofstream trace(dir_ / "keyboard-help.txt");
            // The dialog opens on Done; Tab must then move through it and come back to Done.
            // (A headless window is never active, so GTK doesn't apply the starting focus by itself.)
            GtkWidget* opened = dialog ? adw_dialog_get_focus(ADW_DIALOG(dialog)) : nullptr;
            const bool starts_on_done = opened && Describe(opened) == "Done";
            if (starts_on_done) gtk_widget_grab_focus(opened);
            trace << "opened on " << (opened ? Describe(opened) : "(none)") << "\n";
            for (int step = 0; dialog && starts_on_done && step < 12 && !done; ++step) {
              const gboolean moved = gtk_widget_child_focus(GTK_WIDGET(window_->Window()), GTK_DIR_TAB_FORWARD);
              GtkWidget* focus = gtk_root_get_focus(GTK_ROOT(window_->Window()));
              const std::string name = focus ? Describe(focus) : "";
              trace << (moved ? "moved " : "stuck ") << (focus ? G_OBJECT_TYPE_NAME(focus) : "(none)") << " | " << name;
              if (focus && !GTK_IS_BUTTON(focus)) {  // where a non-control focus sits
                trace << " | css=" << gtk_widget_get_css_name(focus) << " path=";
                for (GtkWidget* p = gtk_widget_get_parent(focus); p && p != dialog; p = gtk_widget_get_parent(p))
                  trace << G_OBJECT_TYPE_NAME(p) << "(" << gtk_widget_get_css_name(p) << ")<";
              }
              trace << "\n";
              if (name != "Done" && !name.empty()) seen = name;  // Tab left Done for the content…
              done = !seen.empty() && name == "Done";              // …and came back round to it
            }
            results_ << "keyboard_help=" << (done ? "PASS" : "FAIL") << " opened=" << (opened ? Describe(opened) : "(none)")
                     << " then=" << seen << "\n";
            if (!done) failed_ = true;
            window_->CloseHelp();
            Later(600, [this] {
              window_->ShowSetup(0);
              Later(1000, [this] {
                Walk("setup", {"Language", "Next"});
                gtk_window_destroy(window_->Window());
                window_.reset();
                Finish();
              });
            });
          });
        });
        });
      });
    });
  }

  void Walk(const std::string& page, std::initializer_list<const char*> required) {
    if (g_getenv("SUKUT_FOCUS_DEBUG")) DumpFocusables(GTK_WIDGET(window_->Window()));
    const auto stops = FocusWalk(GTK_WIDGET(window_->Window()), window_->FocusStart());
    std::ofstream out(dir_ / ("keyboard-" + page + ".txt"));
    std::set<std::string> names;
    int unnamed = 0;
    for (const auto& stop : stops) {
      out << stop << "\n";
      const auto bar = stop.find(" | ");
      const std::string name = stop.substr(bar + 3);
      if (name.empty()) ++unnamed;
      names.insert(name);
    }
    std::string missing;
    for (const char* need : required) {
      const bool found = std::any_of(names.begin(), names.end(), [&](const std::string& n) { return n.find(need) != std::string::npos; });
      if (!found) missing += std::string(missing.empty() ? "" : ", ") + need;
    }
    const bool ok = missing.empty() && unnamed == 0;
    if (!ok) failed_ = true;
    results_ << "keyboard_" << page << "=" << (ok ? "PASS" : "FAIL") << " stops=" << stops.size() << " unnamed=" << unnamed
             << (missing.empty() ? "" : " missing=" + missing) << "\n";
  }

  void Finish() {
    std::ofstream(dir_ / "capture-results.txt", std::ios::app) << results_.str();
    g_print("%s", results_.str().c_str());
    exit_code_ = failed_ ? 1 : 0;
    g_application_quit(G_APPLICATION(app_));
  }

 public:
  int exit_code_ = 0;

 private:
  AdwApplication* app_;
  std::filesystem::path dir_;
  std::vector<Scene> scenes_;
  bool keyboard_;
  std::size_t index_ = 0;
  std::unique_ptr<SukutWindow> window_;
  std::ostringstream results_;
  bool failed_ = false;
};

std::vector<Scene> MainScenes() {
  auto settings = [](SukutWindow& w) { w.ShowPage(SukutWindow::Page::Settings); };
  auto about = [](SukutWindow& w) { w.ScrollSettings(true); };
  auto help = [](SukutWindow& w) { w.OpenHelp(); };
  auto attention = [] { HostState s = Base("en", "dark"); s.state = 2; s.attention = "source_closed"; return s; }();
  auto no_tray = [] { HostState s = Base("en", "dark"); s.tray_available = false; return s; }();
  return {
      {"01-audio-en-dark-off", 1040, 720, Show(Base("en", "dark")), {}},
      {"02-audio-en-dark-on", 1040, 720, Show(On(Base("en", "dark"), 1)), {}},
      {"03-audio-en-dark-standby-optimized", 1040, 720, Show(On(Base("en", "dark"), 2, true)), {}},
      {"04-audio-en-dark-attention", 1040, 720, Show(attention), {}},
      {"05-audio-en-light-on-strict", 1040, 720, Show(On(Base("en", "light"), 3)), {}},
      {"06-audio-ar-dark-off", 1040, 720, Show(Base("ar", "dark")), {}},
      {"07-audio-ar-light-standby", 1040, 720, Show(On(Base("ar", "light"), 1, true)), {}},
      {"08-audio-en-medium", 760, 720, Show(Base("en", "dark")), {}},
      {"09-audio-en-minimum", 420, 480, Show(Base("en", "dark")), {}},
      {"10-sidebar-en-minimum", 420, 480, Show(Base("en", "dark"), [](SukutWindow& w) { w.ShowSidebar(); }), {}},
      {"11-audio-ar-minimum", 420, 480, Show(On(Base("ar", "dark"), 2)), {}},
      {"12-settings-en-dark", 1040, 720, Show(Base("en", "dark"), settings), {}},
      {"13-settings-en-dark-about", 1040, 720, Show(Base("en", "dark"), settings), about},
      {"14-settings-ar-light-about", 1040, 720, Show(Base("ar", "light"), settings), about},
      {"15-settings-en-no-tray", 1040, 720, Show(no_tray, settings), {}},
      {"16-help-en-dark", 1040, 720, Show(Base("en", "dark"), help), {}},
      {"17-help-ar-light", 1040, 720, Show(Base("ar", "light"), help), {}},
      {"18-help-en-minimum", 420, 480, Show(Base("en", "dark"), help), {}},
      {"19-setup-1-en-dark", 1040, 720, Show(Base("en", "dark"), [](SukutWindow& w) { w.ShowSetup(0); }), {}},
      {"20-setup-2-en-dark", 1040, 720, Show(Base("en", "dark"), [](SukutWindow& w) { w.ShowSetup(1); }), {}},
      {"21-setup-2-en-no-tray", 1040, 720, Show(no_tray, [](SukutWindow& w) { w.ShowSetup(1); }), {}},
      {"22-setup-3-en-dark", 1040, 720, Show(Base("en", "dark"), [](SukutWindow& w) { w.ShowSetup(2); }), {}},
      {"23-setup-4-en-dark", 1040, 720, Show(Base("en", "dark"), [](SukutWindow& w) { w.ShowSetup(3); }), {}},
      {"24-setup-1-ar-light", 1040, 720, Show(Base("ar", "light"), [](SukutWindow& w) { w.ShowSetup(0); }), {}},
      {"25-setup-4-ar-dark", 1040, 720, Show(Base("ar", "dark"), [](SukutWindow& w) { w.ShowSetup(3); }), {}},
  };
}

// EasyEffects running (its sink is an output) and the notices filtering survives.
std::vector<Scene> EasyEffectsScenes() {
  const auto with_easyeffects = [](HostState state, std::function<void(SukutWindow&)> then = {}) {
    return [state, then](SukutWindow& w) {
      Catalog c = Devices();
      c.outputs.push_back({"easyeffects_sink", "Easy Effects Sink"});
      w.Apply(c);
      w.Apply(state);
      if (then) then(w);
    };
  };
  const auto notice = [](HostState s, const std::string& text) { s.notice = text; return s; };
  const auto help = [](SukutWindow& w) { w.OpenEasyEffectsHelp(); };
  return {
      {"60-audio-en-dark-easyeffects", 1040, 720, with_easyeffects(Base("en", "dark")), {}},
      {"61-audio-en-dark-loop-notice", 1040, 720, with_easyeffects(notice(On(Base("en", "dark"), 1), "feedback_loop:Easy Effects Sink")), {}},
      {"62-audio-ar-light-process-all-notice", 1040, 720,
       with_easyeffects(notice(On(Base("ar", "light"), 2), "easyeffects_process_all:Firefox")), {}},
      {"63-audio-en-minimum-pinned-notice", 420, 480, with_easyeffects(notice(On(Base("en", "dark"), 1), "stream_pinned:Firefox")), {}},
      {"64-easyeffects-help-en-dark", 1040, 720, with_easyeffects(Base("en", "dark"), help), {}},
      {"65-easyeffects-help-ar-light", 1040, 720, with_easyeffects(Base("ar", "light"), help), {}},
      {"66-easyeffects-help-en-minimum", 420, 480, with_easyeffects(Base("en", "dark"), help), {}},
  };
}

// A snap whose permissions aren't connected yet.
std::vector<Scene> SnapScenes() {
  const std::string command = "sudo snap connect sukut:pipewire && sudo snap connect sukut:audio-record && sudo snap connect sukut:raw-usb";
  const auto missing = [command](SukutWindow& w) { w.SetSnapAccessForTest(command, true); };
  const auto dialog = [missing](SukutWindow& w) { missing(w); w.OpenSnapAccessHelp(); };
  const auto setup = [missing](SukutWindow& w) { missing(w); w.ShowSetup(1); };
  return {
      {"70-snap-banner-en-dark", 1040, 720, Show(Base("en", "dark"), missing), {}},
      {"71-snap-dialog-en-dark", 1040, 720, Show(Base("en", "dark"), dialog), {}},
      {"72-snap-dialog-ar-light", 1040, 720, Show(Base("ar", "light"), dialog), {}},
      {"73-snap-setup-en-dark", 1040, 720, Show(Base("en", "dark"), setup), {}},
      {"74-snap-setup-ar-light", 1040, 720, Show(Base("ar", "light"), setup), {}},
      {"75-snap-dialog-en-minimum", 420, 480, Show(Base("en", "dark"), dialog), {}},
  };
}

using Task = SukutWindow::PhoneTask;
using Severity = SukutWindow::Severity;

PhoneDevice Pixel() { return {"R5CT10A2B3C", "device", "Pixel 8"}; }
PhoneInfo PixelInfo(bool installed, bool ready) {
  PhoneInfo info{"Pixel 8", "15", installed ? "0.4" : "", std::nullopt, ready};
  if (installed) info.sukut_code = 54;
  return info;
}

// The Phone page in a given task, step and connection state.
std::function<void(SukutWindow&)> Phone(HostState state, Task task, int step, std::vector<PhoneDevice> devices, std::optional<PhoneInfo> info,
                                        std::string message = "", Severity severity = Severity::None) {
  return Show(state, [=](SukutWindow& w) {
    w.ShowPage(SukutWindow::Page::Phone);
    w.ShowPhoneTask(task, step);
    w.SetPhoneState(devices, info, message, severity);
  });
}

std::vector<Scene> PhoneScenes() {
  const auto en = Base("en", "dark"), light = Base("en", "light"), ar = Base("ar", "dark"), ar_light = Base("ar", "light");
  const std::vector<PhoneDevice> one{Pixel()};
  return {
      {"40-phone-en-dark-no-phone", 1040, 720, Phone(en, Task::Choices, 0, {}, std::nullopt), {}},
      {"41-phone-en-dark-ready", 1040, 720, Phone(en, Task::Choices, 0, one, PixelInfo(true, true)), {}},
      {"42-phone-en-dark-setup-1", 1040, 720, Phone(en, Task::FullSetup, 0, one, PixelInfo(false, false)), {}},
      {"43-phone-en-dark-setup-2", 1040, 720, Phone(en, Task::FullSetup, 1, one, PixelInfo(false, false)), {}},
      {"44-phone-en-light-setup-3-installed", 1040, 720,
       Phone(light, Task::FullSetup, 2, one, PixelInfo(true, false), "Sukut installed. Continue to open it on the phone.", Severity::Success), {}},
      {"45-phone-en-dark-setup-4-activated", 1040, 720,
       Phone(en, Task::FullSetup, 3, one, PixelInfo(true, true),
             "Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart.", Severity::Success),
       {}},
      {"46-phone-en-dark-setup-5-ready", 1040, 720, Phone(en, Task::FullSetup, 4, one, PixelInfo(true, true)), {}},
      {"47-phone-en-dark-install-refused", 1040, 720,
       Phone(en, Task::Install, 0, one, PixelInfo(false, false), "Failure [INSTALL_FAILED_USER_RESTRICTED: Install canceled by user]", Severity::Warning),
       {}},
      {"48-phone-en-dark-usb-help-unauthorized", 1040, 720,
       Phone(en, Task::UsbHelp, 0, {{"R5CT10A2B3C", "unauthorized", "Android"}}, std::nullopt), {}},
      {"49-phone-ar-dark-activation-needed", 1040, 720, Phone(ar, Task::Choices, 0, one, PixelInfo(true, false)), {}},
      {"50-phone-ar-light-activated", 1040, 720,
       Phone(ar_light, Task::Activate, 0, one, PixelInfo(true, true),
             "Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart.", Severity::Success),
       {}},
      {"51-phone-en-minimum", 420, 480, Phone(en, Task::Choices, 0, one, PixelInfo(true, true)), {}},
      {"52-phone-en-dark-two-phones", 1040, 720,
       Phone(en, Task::Choices, 0, {Pixel(), {"9A281FFAZ0", "device", "Pixel 8a"}}, std::nullopt), {}},
      // Linux only: adb can't open the phone yet; Allow access appears on the card.
      {"53-phone-en-dark-no-permissions", 1040, 720,
       Phone(en, Task::UsbHelp, 0, {{"R5CT10A2B3C", "no permissions", "Android"}}, std::nullopt,
             "Sukut can't access the phone yet. Choose Allow access.", Severity::Warning),
       {}},
      {"54-phone-ar-light-no-permissions", 1040, 720,
       Phone(ar_light, Task::Choices, 0, {{"R5CT10A2B3C", "no permissions", "Android"}}, std::nullopt), {}},
  };
}

std::vector<Scene> ContrastScenes() {
  auto settings = [](SukutWindow& w) { w.ShowPage(SukutWindow::Page::Settings); };
  return {
      {"30-audio-en-contrast-off", 1040, 720, Show(Base("en", "light")), {}},
      {"31-audio-en-contrast-standby", 1040, 720, Show(On(Base("en", "light"), 2, true)), {}},
      {"32-audio-ar-contrast-dark-on", 1040, 720, Show(On(Base("ar", "dark"), 1)), {}},
      {"33-settings-en-contrast-about", 1040, 720, Show(Base("en", "light"), settings), [](SukutWindow& w) { w.ScrollSettings(true); }},
      {"34-help-en-contrast", 1040, 720, Show(Base("en", "light"), [](SukutWindow& w) { w.OpenHelp(); }), {}},
  };
}

}  // namespace

int RunCapture(AdwApplication* app, const std::string& directory, const std::string& set) {
  std::filesystem::create_directories(directory);
  const bool contrast = set == "contrast";
  auto runner = std::make_shared<Runner>(app, directory,
                                         contrast ? ContrastScenes() : set == "keyboard" ? std::vector<Scene>{} : set == "phone" ? PhoneScenes()
                                         : set == "easyeffects" ? EasyEffectsScenes() : set == "snap" ? SnapScenes() : MainScenes(),
                                         !contrast && set != "phone" && set != "easyeffects" && set != "snap");
  g_signal_connect_data(app, "activate", G_CALLBACK(+[](GApplication* a, gpointer data) {
    InstallStyle();
    // Still frames: dialogs open at once and the credit holds its first colours.
    g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
    g_application_hold(a);
    static_cast<Runner*>(data)->Next();
  }), runner.get(), nullptr, G_CONNECT_DEFAULT);
  char* argv[] = {const_cast<char*>("sukut"), nullptr};
  const int status = g_application_run(G_APPLICATION(app), 1, argv);
  return status != 0 ? status : runner->exit_code_;
}

}  // namespace sukut::window
