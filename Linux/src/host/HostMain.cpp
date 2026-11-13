// sukut-host: Sukut's always-running part on Linux (Linux/PLAN.md section 6).
//   sukut-host [--tray] [--engine DIR] [--icon-theme-path DIR]
//              [--window-command CMD] [--autostart-command CMD]
//   sukut-host --restore-audio      undo routing left by a crash, then exit
// It owns the engine, the tray and the window's socket, and always starts Off.
// Without --tray it opens the window at start, as Sukut.exe does on Windows.
// One host per user: a second launch asks the running one to open the window.
//
// Socket requests (fields joined by '\x1f'; replies are '1' + payload or '0' + error):
//   hello                                    a window connected; the tray offers only Open and Exit
//   read                                     engine state and settings (JSON)
//   apps                                     real outputs and playing apps (JSON)
//   tray-catalog                             cached catalog; no discovery before showing the tray
//   select MODE OUTPUT PID BUFFERING BG_DB   apply a mode (0 = Off) with that route and save it; from Off,
//                                            the output follows a system choice made while Off (PrepareStart)
//   amp DB                                   amplification, 0 to 12 dB
//   settings KEY=VALUE...                    Language, Theme, CloseToTray, SetupCompleted, StartWithSystem,
//                                            and while Off: PreferredMode, Output, SourcePid, Buffering,
//                                            OptimizedBackgroundDb (saved for the next Start)
//   refresh                                  re-read outputs and apps
//   open [TOKEN]                             open or raise the window (XDG activation token)
//   exit                                     restore audio and quit
// The host is the only writer of settings.json, so the window and tray never race.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <malloc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>

#include "audio/PipeWireBridge.h"
#include "audio/Sandbox.h"
#include "host/Desktop.h"
#include "host/HostEngine.h"
#include "host/HostServer.h"
#include "host/HostSettings.h"
#include "host/Tray.h"

namespace {

using namespace sukut::host;
using sukut::linux_audio::PipeWireBridge;

struct Options {
  std::filesystem::path engine;
  bool tray_only = false;
  std::string icon_theme_path;
  std::string window_command;
  std::string autostart_command = "sukut --tray";
};

// A development build keeps its data in <build>/share/sukut beside <build>/bin;
// an installed one uses /usr/share/sukut and the system icon theme.
std::filesystem::path ExeDir() {
  std::error_code code;
  return std::filesystem::read_symlink("/proc/self/exe", code).parent_path();
}

void FillDefaults(Options& o) {
  const auto staged = ExeDir() / ".." / "share" / "sukut";
  if (o.engine.empty()) {
    const char* env = std::getenv("SUKUT_ENGINE_DIR");
    o.engine = env && *env ? std::filesystem::path(env)
               : std::filesystem::exists(staged / "engine") ? staged / "engine"
                                                             : sukut::InstallPrefix() / "share/sukut/engine";
  }
  if (o.icon_theme_path.empty() && std::filesystem::exists(staged / "icons")) o.icon_theme_path = (staged / "icons").lexically_normal().string();
  if (o.window_command.empty()) o.window_command = std::filesystem::exists(ExeDir() / "sukut") ? (ExeDir() / "sukut").string() : "sukut";
}

void Log(const std::string& line) { std::cerr << "sukut-host: " << line << std::endl; }

// Runs a task on the main loop later, never inside the caller (GDBus and socket
// callbacks may be mid-way through their own state).
void Later(std::function<void()> task) {
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()>*>(data))();
        return G_SOURCE_REMOVE;
      },
      new std::function<void()>(std::move(task)), [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}

const char* T(bool arabic, const char* english, const char* arabic_text) { return arabic ? arabic_text : english; }

std::string ModeName(int mode, bool ar) {
  switch (mode) {
    case 1: return T(ar, "Music Filter", "مرشّح الموسيقى");
    case 2: return T(ar, "Optimized", "محسّن");
    case 3: return T(ar, "Strict", "صارم");
    default: return T(ar, "Off", "متوقف");
  }
}

// The tray status line; the same texts as the Windows tray (StatusText).
std::string StatusText(const EngineState& e, bool ar) {
  if (e.busy) return T(ar, "Switching…", "جارٍ التبديل…");
  if (e.reconnecting) return T(ar, "Reconnecting", "إعادة الاتصال");
  if (e.state == 2) return T(ar, "Attention · open Sukut", "تنبيه · افتح سُكوت");
  if (e.mode == 0) return T(ar, "Off", "متوقف");
  if (e.standby) return T(ar, "Standby · nothing playing", "استعداد · لا يوجد صوت");
  return std::string(T(ar, "Active · ", "نشط · ")) + ModeName(e.mode, ar);
}

bool ProcessExists(std::uint32_t pid) { return std::filesystem::exists("/proc/" + std::to_string(pid)); }

std::string Flag(bool value) { return value ? "true" : "false"; }

// The output chosen in the system's sound settings: WirePlumber's configured
// default (kept while that device is missing), else the device in use now.
std::string SystemChoice(const PipeWireBridge::Catalog& c) {
  return !c.configured_output.empty() ? c.configured_output : c.default_output;
}

std::string CatalogJson(const PipeWireBridge::Catalog& c) {
  std::ostringstream j;
  j << "{\"outputs\":[";
  for (std::size_t i = 0; i < c.outputs.size(); ++i)
    j << (i ? "," : "") << "{\"name\":\"" << JsonEscape(c.outputs[i].name) << "\",\"description\":\""
      << JsonEscape(c.outputs[i].description) << "\"}";
  j << "],\"apps\":[";
  for (std::size_t i = 0; i < c.apps.size(); ++i)
    j << (i ? "," : "") << "{\"pid\":" << c.apps[i].pid << ",\"name\":\"" << JsonEscape(c.apps[i].name) << "\"}";
  j << "],\"defaultOutput\":\"" << JsonEscape(c.default_output) << "\"}";
  return j.str();
}

class Host {
 public:
  explicit Host(Options options) : options_(std::move(options)), engine_(options_.engine) {}
  int Run();

 private:
  // Socket thread.
  std::string Handle(const std::vector<std::string>& f);
  std::string ReadJson();
  bool ReadCatalog(PipeWireBridge::Catalog* out = nullptr);
  // Main thread.
  Tray::Actions TrayActions();
  void OnWindow(bool connected, bool exit_requested);
  void Supervise();
  void Refresh();
  void Select(int mode);
  void Reapply();
  void PrepareStart();
  void NotifyOutput(const std::string& description);
  void NotifyNotice(const std::string& notice);
  void OpenWindow();
  void OpenTray(int x,int y);
  void BeginExit();
  void TrimIfOff();
  // Any thread.
  void Change(const std::function<void(HostSettings&)>& change, bool routing);
  HostSettings Settings() const;
  void RecordSystemChoice();

  Options options_;
  HostEngine engine_;
  std::unique_ptr<HostServer> server_;
  std::unique_ptr<Tray> tray_;
  GMainLoop* loop_ = nullptr;
  GDBusConnection* bus_ = nullptr;

  mutable std::mutex mutex_;  // settings_ and catalog_
  HostSettings settings_;
  PipeWireBridge::Catalog catalog_;

  // Main thread only.
  int windows_ = 0;
  GPid tray_popup_pid_ = 0;
  std::string token_;
  std::uint64_t shown_revision_ = ~0ULL;
  int last_mode_ = 0;
  std::string notified_output_;
  std::chrono::steady_clock::time_point notified_at_{};
  std::set<std::string> notified_notices_;  // once per session

  std::atomic<bool> tray_available_{false}, exiting_{false};
  std::atomic<std::uint64_t> raise_requests_{0};  // snap: a second launch asks the window to come forward
};

HostSettings Host::Settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

void Host::Change(const std::function<void(HostSettings&)>& change, bool routing) {
  std::lock_guard lock(mutex_);
  change(settings_);
  if (routing) ++settings_.host_settings_version;
  std::string error;
  if (!settings_.Save(error)) Log(error);
}

bool Host::ReadCatalog(PipeWireBridge::Catalog* out) {
  PipeWireBridge::Catalog catalog;
  std::string error;
  if (!PipeWireBridge::ReadCatalog(catalog, error)) { Log("cannot read outputs and apps: " + error); return false; }
  if (out) *out = catalog;
  std::lock_guard lock(mutex_);
  catalog_ = std::move(catalog);
  return true;
}

int Host::Run() {
  loop_ = g_main_loop_new(nullptr, FALSE);
  std::string message;
  if (!PipeWireBridge::RestoreFromJournal(message)) Log("cannot restore audio from the journal: " + message);
  else if (message != "nothing to restore") Log("restored audio left by an earlier run: " + message);

  settings_ = HostSettings::Load();
  // A saved source app that no longer exists would only route nothing.
  if (settings_.source_pid != 0 && !ProcessExists(settings_.source_pid)) Change([](HostSettings& s) { s.source_pid = 0; }, true);
  UseRealtimeKit();

  GError* error = nullptr;
  bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (bus_ == nullptr) {
    Log(std::string("no session bus, so no tray or notifications: ") + (error ? error->message : "unknown"));
    if (error) g_error_free(error);
  } else {
    tray_ = std::make_unique<Tray>(TrayActions(), options_.icon_theme_path);
    std::string tray_error;
    if (!tray_->Start(bus_, tray_error)) { Log("tray: " + tray_error); tray_.reset(); }
  }

  server_ = std::make_unique<HostServer>(
      HostServer::DefaultPath(), [this](const std::vector<std::string>& f) { return Handle(f); },
      [this](bool connected, bool exit_requested) { Later([this, connected, exit_requested] { OnWindow(connected, exit_requested); }); });
  std::string server_error;
  if (!server_->Start(server_error)) { Log(server_error); return 1; }

  ReadCatalog();
  Refresh();
  g_timeout_add_seconds(1, [](gpointer self) -> gboolean { static_cast<Host*>(self)->Supervise(); return G_SOURCE_CONTINUE; }, this);
  for (const int stop_signal : {SIGTERM, SIGINT, SIGHUP})
    g_unix_signal_add(stop_signal, [](gpointer self) -> gboolean { static_cast<Host*>(self)->BeginExit(); return G_SOURCE_CONTINUE; }, this);
  if (!options_.tray_only) {
    OpenWindow();
  } else {
    // Started for the tray, but no tray appeared (stock GNOME without the
    // AppIndicator extension): show the window so Sukut is not invisible.
    g_timeout_add_seconds(5, [](gpointer data) -> gboolean {
      auto* self = static_cast<Host*>(data);
      if (!self->tray_available_ && self->windows_ == 0) { Log("no tray: opening the window"); self->OpenWindow(); }
      return G_SOURCE_REMOVE;
    }, this);
  }
  Log("ready socket=" + HostServer::DefaultPath().string() + (tray_ ? " tray=" + tray_->BusName() : std::string(" tray=none")));
  g_main_loop_run(loop_);

  tray_.reset();
  if (bus_) { g_dbus_connection_flush_sync(bus_, nullptr, nullptr); g_object_unref(bus_); }
  g_main_loop_unref(loop_);
  Log("stopped");
  return 0;
}

Tray::Actions Host::TrayActions() {
  Tray::Actions a;
  a.activate = [this] { OpenWindow(); };
  a.open = [this] { OpenWindow(); };
  a.popup = [this](int x,int y){OpenTray(x,y);};
  a.exit = [this] { Later([this] { BeginExit(); }); };
  a.power = [this](bool on) { Select(on ? Settings().preferred_mode : 0); };
  a.select_mode = [this](int mode) { Select(mode); };
  a.set_buffering = [this](int level) { Change([level](HostSettings& s) { s.buffering = level; }, true); Reapply(); };
  a.set_output = [this](const std::string& name) {
    Change([&name](HostSettings& s) { s.output = name; }, true);
    RecordSystemChoice();  // while Off: an explicit choice made against today's system output
    Reapply();
  };
  a.set_source = [this](std::uint32_t pid) { Change([pid](HostSettings& s) { s.source_pid = pid; }, true); Reapply(); };
  a.refresh = [this] { ReadCatalog(); Refresh(); TrimIfOff(); };
  a.activation_token = [this](const std::string& token) { token_ = token; };
  a.availability = [this](bool available) {
    tray_available_ = available;
    Log(available ? "tray: registered" : "tray: no tray host");
  };
  return a;
}

void Host::Refresh() {
  if (!tray_) return;
  const EngineState e = engine_.State();
  Tray::State t;
  {
    std::lock_guard lock(mutex_);
    const bool ar = settings_.Arabic();
    t.arabic = ar;
    t.window_open = windows_ > 0;
    t.on = e.mode != 0;
    t.standby = e.mode != 0 && e.standby;
    t.attention = e.state == 2;
    t.busy = e.busy;
    t.mode = e.mode;
    t.preferred = settings_.preferred_mode;
    t.buffering = settings_.buffering;
    t.status = StatusText(e, ar);
    for (const auto& o : catalog_.outputs) t.outputs.emplace_back(o.name, o.description);
    t.output = !e.output.empty() ? e.output : !settings_.output.empty() ? settings_.output : catalog_.default_output;
    for (const auto& app : catalog_.apps) t.apps.emplace_back(app.pid, app.name);
    t.source_pid = settings_.source_pid;
    // As on Windows, a chosen app that is not playing now stays listed.
    if (t.source_pid != 0 && std::none_of(t.apps.begin(), t.apps.end(), [&](const auto& a) { return a.first == t.source_pid; }))
      t.apps.emplace_back(t.source_pid, T(ar, "Selected app (unavailable)", "التطبيق المحدد (غير متاح)"));
  }
  shown_revision_ = e.revision;
  tray_->Update(t);
}

void Host::Supervise() {
  if (exiting_) return;
  for (const auto& fact : engine_.Supervise()) {
    // Sukut followed an output the user wants, or the chosen app closed: keep that.
    if (fact.rfind("output=", 0) == 0) Change([&fact](HostSettings& s) { s.output = fact.substr(7); }, true);
    else if (fact == "source_closed") Change([](HostSettings& s) { s.source_pid = 0; }, true);
    else if (fact.rfind("connected=", 0) == 0) NotifyOutput(fact.substr(10));
    else if (fact.rfind("notice=", 0) == 0) NotifyNotice(fact.substr(7));
  }
  const int mode = engine_.State().mode;
  if (last_mode_ != 0 && mode == 0) {
    // Stopped: Sukut gave the default output back. A different system choice at
    // the next Start means the user changed it while Sukut was Off.
    notified_notices_.clear();
    RecordSystemChoice();
  }
  last_mode_ = mode;
  if (engine_.State().revision != shown_revision_) Refresh();
}

// Windows 0.4.2: Sukut's output follows the system's choice made while it was
// Off (headphones picked in the sound settings, or connected and made the default
// by the desktop). An output chosen in Sukut since then is kept. Settings from
// before 0.4.0~beta.7 have no record and follow the system once.
void Host::PrepareStart() {
  if (engine_.State().mode != 0) return;
  PipeWireBridge::Catalog catalog;
  if (!ReadCatalog(&catalog)) return;
  const std::string system = SystemChoice(catalog);
  if (system.empty()) return;
  Change([&](HostSettings& s) {
    if (system != s.output_default && system != s.output) {
      Log("the system output changed while Off: starting on " + system);
      s.output = system;
    }
    s.output_default = system;
  }, true);
}

// While Off: the system output that Sukut's saved choice now stands against.
void Host::RecordSystemChoice() {
  if (engine_.State().mode != 0) return;
  PipeWireBridge::Catalog catalog;
  if (!ReadCatalog(&catalog)) return;
  const std::string system = SystemChoice(catalog);
  if (!system.empty() && system != Settings().output_default) Change([&](HostSettings& s) { s.output_default = system; }, false);
}

// The Windows notice for an automatic switch to a newly connected output; the
// same output again within 5 s is not announced twice.
void Host::NotifyOutput(const std::string& description) {
  const auto now = std::chrono::steady_clock::now();
  if (description == notified_output_ && now - notified_at_ < std::chrono::seconds(5)) return;
  notified_output_ = description;
  notified_at_ = now;
  const bool ar = Settings().Arabic();
  Notify(bus_, T(ar, "Audio output switched", "تم تبديل مخرج الصوت"), std::string(T(ar, "Sukut now uses ", "يستخدم سُكوت الآن ")) + description);
  Log("switched to a newly connected output: " + description);
}

// Problems filtering survives (EngineState::notice), told once per session; the
// window shows the steps to fix them.
void Host::NotifyNotice(const std::string& notice) {
  const auto colon = notice.find(':');
  const std::string code = notice.substr(0, colon), detail = colon == std::string::npos ? "" : notice.substr(colon + 1);
  Log("notice: " + notice);
  if (!notified_notices_.insert(code).second) return;
  const bool ar = Settings().Arabic();
  if (code == "feedback_loop")
    Notify(bus_, T(ar, "Sukut stopped an audio loop", "أوقف سُكوت حلقة صوتية"),
           detail + T(ar, " sent Sukut's sound back into Sukut, so Sukut now plays to another output. Open Sukut to see how to fix it.",
                 " يعيد صوت سُكوت إلى سُكوت، لذا يشغّل سُكوت الصوت الآن على مخرج آخر. افتح سُكوت لمعرفة طريقة الإصلاح."));
  else if (code == "easyeffects_process_all")
    Notify(bus_, T(ar, "EasyEffects takes apps before Sukut", "يأخذ EasyEffects التطبيقات قبل سُكوت"),
           T(ar, "Turn off “Process all output streams” in EasyEffects. Open Sukut for the steps.",
             "أوقف خيار «Process all output streams» في EasyEffects. افتح سُكوت لمعرفة الخطوات."));
  else if (code == "stream_pinned")
    Notify(bus_, T(ar, "An app skips Sukut", "تطبيق يتجاوز سُكوت"),
           detail + T(ar, " is set to play on a specific output, so Sukut can't filter it. Open Sukut for the steps.",
                      " مضبوط على مخرج صوت محدد، لذا لا يستطيع سُكوت ترشيحه. افتح سُكوت لمعرفة الخطوات."));
}

void Host::Select(int mode) {
  if (exiting_) return;
  if (mode > 0) PrepareStart();
  if (mode > 0) Change([mode](HostSettings& s) { s.preferred_mode = mode; }, true);
  engine_.SelectAsync(mode, Settings(), [this](bool ok, std::string error) {
    if (!ok) Log("cannot start: " + error);
    Later([this] { Refresh(); });
  });
}

void Host::Reapply() {
  // A new route while Sukut runs restarts it on that route; while Off it is only saved.
  const int mode = engine_.State().mode;
  if (mode != 0) Select(mode);
  else Refresh();
}

void Host::OpenTray(int x,int y) {
  if(exiting_)return;
  if(tray_popup_pid_>0){::kill(tray_popup_pid_,SIGTERM);return;}
  gchar** parsed=nullptr;GError* error=nullptr;
  if(!g_shell_parse_argv(options_.window_command.c_str(),nullptr,&parsed,&error)){if(error)g_error_free(error);return;}
  std::vector<gchar*> args;for(auto**p=parsed;*p;++p)args.push_back(*p);
  std::string sx=std::to_string(x),sy=std::to_string(y),snapshot=ReadJson();
  args.push_back(const_cast<gchar*>("--tray-popup"));args.push_back(sx.data());args.push_back(sy.data());args.push_back(const_cast<gchar*>("--tray-state"));args.push_back(snapshot.data());args.push_back(nullptr);
  if(g_spawn_async(nullptr,args.data(),nullptr,static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD),nullptr,nullptr,&tray_popup_pid_,&error)) {
    g_child_watch_add(tray_popup_pid_,+[](GPid pid,gint status,gpointer data){auto*self=static_cast<Host*>(data);self->tray_popup_pid_=0;g_spawn_close_pid(pid);if(WIFEXITED(status)&&WEXITSTATUS(status)==77)self->tray_->SetCustomMenu(false);},this);
  }else {if(error){Log(error->message);g_error_free(error);}tray_->SetCustomMenu(false);}
  g_strfreev(parsed);
}

void Host::OpenWindow() {
  if (exiting_) return;
  gchar** argv = nullptr;
  GError* error = nullptr;
  if (!g_shell_parse_argv(options_.window_command.c_str(), nullptr, &argv, &error)) {
    Log(std::string("bad window command: ") + error->message);
    g_error_free(error);
    return;
  }
  // The window is a single-instance GtkApplication: launching it again raises
  // the open one. The activation token lets Wayland give it focus.
  gchar** environment = g_get_environ();
  if (!token_.empty()) environment = g_environ_setenv(environment, "XDG_ACTIVATION_TOKEN", token_.c_str(), TRUE);
  token_.clear();
  if (!g_spawn_async(nullptr, argv, environment, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &error)) {
    Log(std::string("cannot open the window: ") + error->message);
    g_error_free(error);
  }
  g_strfreev(environment);
  g_strfreev(argv);
}

void Host::OnWindow(bool connected, bool exit_requested) {
  if (connected) { ++windows_; Refresh(); return; }
  windows_ = std::max(0, windows_ - 1);
  if (exiting_ || exit_requested || windows_ > 0) { if (!exiting_) Refresh(); return; }
  const HostSettings s = Settings();
  if (!s.close_to_tray || !tray_available_) {
    // No tray to return to (or the user chose so): closing the window exits Sukut.
    BeginExit();
    return;
  }
  Refresh();
  const bool ar = s.Arabic();
  const bool off = engine_.State().mode == 0;
  // The Windows texts without its "under 20 MB": the Linux figure is measured in stage 9.
  Notify(bus_, T(ar, "Sukut is in the tray", "سُكوت في منطقة الإشعارات"),
         off ? T(ar, "The window closed to save memory. Click the س icon to open Sukut.",
                 "أُغلقت النافذة لتوفير الذاكرة. اضغط أيقونة س لفتح سُكوت.")
             : T(ar, "Filtering continues. The window closed to save memory.",
                 "يستمر الترشيح. أُغلقت النافذة لتوفير الذاكرة."));
  Log("window closed to the tray");
  TrimIfOff();
}

void Host::TrimIfOff() {
  // Only while Off: a running filter must never take page faults on its audio thread.
  const auto e = engine_.State();
  if (e.mode == 0 && !e.busy) malloc_trim(0);
}

void Host::BeginExit() {
  if (exiting_.exchange(true)) return;
  Log("exiting");
  if (server_) server_->Stop();  // an open window sees the host go and closes
  engine_.Shutdown();            // restores routing and removes Sukut's nodes
  {
    std::lock_guard lock(mutex_);
    std::string error;
    if (!settings_.Save(error)) Log(error);
  }
  g_main_loop_quit(loop_);
}

std::string Host::ReadJson() {
  const EngineState e = engine_.State();
  std::lock_guard lock(mutex_);
  const HostSettings& s = settings_;
  std::ostringstream j;
  j << "{\"host\":1,\"mode\":" << e.mode << ",\"state\":" << e.state << ",\"busy\":" << Flag(e.busy)
    << ",\"standby\":" << Flag(e.standby) << ",\"reconnecting\":" << Flag(e.reconnecting) << ",\"output\":\""
    << JsonEscape(e.output) << "\",\"activeSourcePid\":" << e.source_pid << ",\"activeBuffering\":" << e.buffering
    << ",\"attention\":\"" << JsonEscape(e.attention) << "\",\"notice\":\"" << JsonEscape(e.notice) << "\",\"underruns\":" << e.underruns
    << ",\"fallbacks\":" << e.fallbacks << ",\"prioritySet\":" << Flag(e.priority_set) << ",\"revision\":" << e.revision
    << ",\"status\":\"" << JsonEscape(StatusText(e, s.Arabic())) << "\",\"trayAvailable\":" << Flag(tray_available_)
    << ",\"raiseRequests\":" << raise_requests_.load() << ",\"settingsVersion\":" << s.host_settings_version << ",\"language\":\"" << JsonEscape(s.language)
    << "\",\"theme\":\"" << JsonEscape(s.theme) << "\",\"chosenOutput\":\"" << JsonEscape(s.output)
    << "\",\"sourcePid\":" << s.source_pid << ",\"preferredMode\":" << s.preferred_mode << ",\"buffering\":" << s.buffering
    << ",\"amplificationDb\":" << s.amplification_db << ",\"optimizedBackgroundDb\":" << s.optimized_background_db
    << ",\"advancedOptimized\":" << Flag(s.optimized_mix.advanced)
    << ",\"advancedVoiceDb\":" << s.optimized_mix.voice_db << ",\"advancedMusicDb\":" << s.optimized_mix.music_db
    << ",\"advancedBackgroundDb\":" << s.optimized_mix.background_db
    << ",\"setupCompleted\":" << Flag(s.setup_completed) << ",\"closeToTray\":" << Flag(s.close_to_tray)
    << ",\"startWithSystem\":" << Flag(s.start_with_system) << "}";
  return j.str();
}

std::string Host::Handle(const std::vector<std::string>& f) {
  const auto& verb = f[0];
  const auto arg = [&](std::size_t i) { return i < f.size() ? f[i] : std::string(); };
  const auto number = [&](std::size_t i) { return std::atoll(arg(i).c_str()); };
  const auto ok = [](const std::string& payload) { return "1" + payload; };
  const auto fail = [](const std::string& error) { return "0" + error; };
  if (exiting_) return fail("Sukut is exiting");
  if (verb == "hello") return ok("{\"host\":1}");
  if (verb == "raise") { ++raise_requests_; return ok(""); }
  if (verb == "read") return ok(ReadJson());
  if(verb=="tray-catalog") {
    std::lock_guard lock(mutex_);
    return ok(CatalogJson(catalog_));
  }
  if (verb == "apps" || verb == "refresh") {
    PipeWireBridge::Catalog catalog;
    if (!ReadCatalog(&catalog)) return fail("cannot read outputs and apps");
    Later([this] { Refresh(); });
    return ok(verb == "apps" ? CatalogJson(catalog) : "");
  }
  if (verb == "select") {
    const int mode = std::clamp(static_cast<int>(number(1)), 0, 3);
    const bool starting = mode > 0 && engine_.State().mode == 0;
    const std::string before = Settings().output;
    Change([&](HostSettings& s) {
      if (f.size() >= 6) {
        s.output = arg(2);
        s.source_pid = static_cast<std::uint32_t>(std::max(0LL, number(3)));
        s.buffering = std::clamp(static_cast<int>(number(4)), 0, 2);
        s.optimized_background_db = std::clamp(static_cast<int>(number(5)), -20, 10);
      }
      if (f.size() >= 10) {
        s.optimized_mix.advanced = number(6) != 0;
        s.optimized_mix.voice_db = std::clamp(static_cast<int>(number(7)), -20, 10);
        s.optimized_mix.music_db = std::clamp(static_cast<int>(number(8)), -20, 10);
        s.optimized_mix.background_db = std::clamp(static_cast<int>(number(9)), -20, 10);
      }
      if (mode > 0) s.preferred_mode = mode;
    }, true);
    if (starting) {
      if (f.size() >= 6 && arg(2) != before) RecordSystemChoice();  // chosen in the window just now
      PrepareStart();
    }
    std::string error;
    const bool success = engine_.Select(mode, Settings(), error);
    Later([this] { Refresh(); });
    return success ? ok("") : fail(error);
  }
  if (verb == "amp") {
    const int db = std::clamp(static_cast<int>(number(1)), 0, 12);
    Change([db](HostSettings& s) { s.amplification_db = db; }, false);
    engine_.SetAmplification(db);
    return ok("");
  }
  if (verb == "settings") {
    for (std::size_t i = 1; i < f.size(); ++i) {
      const auto equals = f[i].find('=');
      if (equals == std::string::npos) return fail("bad setting " + f[i]);
      const std::string key = f[i].substr(0, equals), value = f[i].substr(equals + 1);
      const bool on = value == "1" || value == "true";
      if (key == "Language" && (value == "en" || value == "ar")) Change([&](HostSettings& s) { s.language = value; }, false);
      else if (key == "Theme") Change([&](HostSettings& s) { s.theme = value; }, false);
      else if (key == "CloseToTray") Change([on](HostSettings& s) { s.close_to_tray = on; }, false);
      else if (key == "SetupCompleted") Change([on](HostSettings& s) { s.setup_completed = on; }, false);
      else if (key == "PreferredMode") Change([&](HostSettings& s) { s.preferred_mode = std::clamp(std::atoi(value.c_str()), 1, 3); }, true);
      else if (key == "Output") { Change([&](HostSettings& s) { s.output = value; }, true); RecordSystemChoice(); }
      else if (key == "SourcePid") Change([&](HostSettings& s) { s.source_pid = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10)); }, true);
      else if (key == "Buffering") Change([&](HostSettings& s) { s.buffering = std::clamp(std::atoi(value.c_str()), 0, 2); }, true);
      else if (key == "OptimizedBackgroundDb")
        Change([&](HostSettings& s) { s.optimized_background_db = std::clamp(std::atoi(value.c_str()), -20, 10); }, false);
      else if (key == "AdvancedOptimized") Change([&](HostSettings& s) { s.optimized_mix.advanced = on; }, false);
      else if (key == "AdvancedVoiceDb") Change([&](HostSettings& s) { s.optimized_mix.voice_db = std::clamp(std::atoi(value.c_str()), -20, 10); }, false);
      else if (key == "AdvancedMusicDb") Change([&](HostSettings& s) { s.optimized_mix.music_db = std::clamp(std::atoi(value.c_str()), -20, 10); }, false);
      else if (key == "AdvancedBackgroundDb") Change([&](HostSettings& s) { s.optimized_mix.background_db = std::clamp(std::atoi(value.c_str()), -20, 10); }, false);
      else if (key == "StartWithSystem") {
        std::string error;
        if (!SetAutostart(on, options_.autostart_command, error)) return fail(error);
        Change([on](HostSettings& s) { s.start_with_system = on; }, false);
      } else {
        return fail("unknown setting " + key);
      }
    }
    Later([this] { Refresh(); });
    return ok("");
  }
  if (verb == "open") {
    Later([this, token = arg(1)] { if (!token.empty()) token_ = token; OpenWindow(); });
    return ok("");
  }
  if (verb == "exit") {
    Later([this] { BeginExit(); });
    return ok("");
  }
  return fail("unknown request");
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  bool restore = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool value = i + 1 < argc;
    if (arg == "--tray") options.tray_only = true;
    else if (arg == "--restore-audio") restore = true;
    else if (arg == "--engine" && value) options.engine = argv[++i];
    else if (arg == "--icon-theme-path" && value) options.icon_theme_path = argv[++i];
    else if (arg == "--window-command" && value) options.window_command = argv[++i];
    else if (arg == "--autostart-command" && value) options.autostart_command = argv[++i];
    else {
      std::cerr << "usage: sukut-host [--tray] [--engine DIR] [--icon-theme-path DIR] [--window-command CMD]"
                   " [--autostart-command CMD] | --restore-audio\n";
      return 2;
    }
  }
  FillDefaults(options);
  std::signal(SIGPIPE, SIG_IGN);

  // One host per user: $XDG_RUNTIME_DIR/sukut/host.lock.
  const auto runtime = HostServer::DefaultPath().parent_path();
  std::error_code code;
  std::filesystem::create_directories(runtime, code);
  ::chmod(runtime.c_str(), 0700);
  const int lock = ::open((runtime / "host.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock < 0) { Log("cannot open " + (runtime / "host.lock").string()); return 1; }
  if (::flock(lock, LOCK_EX | LOCK_NB) != 0) {
    if (restore) { Log("Sukut is running; exit it first (it restores audio itself)"); return 1; }
    if (options.tray_only) return 0;  // already running: nothing more to do
    std::string request = "open";
    if (const char* token = std::getenv("XDG_ACTIVATION_TOKEN"); token && *token) request += std::string("\x1f") + token;
    // The first host may still be starting: give it a few seconds to listen.
    for (int attempt = 0; attempt < 50; ++attempt) {
      std::string reply;
      if (HostServer::Request(HostServer::DefaultPath(), request, reply) && !reply.empty() && reply[0] == '1') return 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Log("another Sukut host holds the lock but does not answer");
    return 1;
  }
  if (restore) {
    std::string message;
    const bool ok = PipeWireBridge::RestoreFromJournal(message);
    std::cout << (ok ? "RESTORED " : "RESTORE_FAILED ") << message << std::endl;
    return ok ? 0 : 1;
  }
  const int result = Host(std::move(options)).Run();
  ::close(lock);
  return result;
}
