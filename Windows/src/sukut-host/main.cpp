// Sukut.exe — the always-running native host.
//
// Owns the audio engine (SukutEngine.dll), the tray icon and its Windows 11
// flyout, and supervises filtering while no window is open. The full WinUI
// window (SukutWindow.exe) is a separate process started on demand that
// talks to this host over a per-session named pipe and exits completely when
// closed, so no GUI runtime stays in memory while Sukut runs in the tray.
#include <fstream>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include <mutex>

#include "sukut-host/Common.h"
#include "sukut-host/Engine.h"
#include "sukut-host/Json.h"
#include "sukut-host/TrayPopup.h"
#include "sukut-host/TrayCatalog.h"
#include "sukut-host/DefaultOutputChanges.h"
#include "sukut-host/NativeMenu.h"
#include "sukut-host/ModeAppearance.h"

namespace sukut::host {
namespace {

constexpr wchar_t kWindowClass[] = L"SukutHostWindow";
constexpr UINT WM_TRAY = WM_APP + 1, WM_OPEN_REQUEST = WM_APP + 2, WM_ENGINE_STATE = WM_APP + 3,
               WM_COMMAND_DONE = WM_APP + 4, WM_EXIT_REQUEST = WM_APP + 5, WM_ENGINE_READY = WM_APP + 6,
               WM_CLIENT_CHANGED = WM_APP + 7, WM_ENGINE_DESTROYED = WM_APP + 8, WM_READ_REQUEST = WM_APP + 9,
               WM_DEVICE_NOTICE = WM_APP + 10, WM_CATALOG = WM_APP + 11;
constexpr UINT_PTR kSuperviseTimer = 1, kRestartTimer = 2, kExitTimer = 3, kPopupTimer = 4, kForceExitTimer = 5, kDeviceTimer = 6;

// Same budget as the window's AudioRestartPolicy: three attempts with 1/2/4 s
// back-off; a minute of healthy playback restores the budget.
class RestartPolicy {
 public:
  bool Request(ULONGLONG now) {
    healthy_since_.reset();
    if (due_) return true;
    if (attempts_ >= 3) return false;
    due_ = now + (1000ULL << attempts_);
    return true;
  }
  bool TryBegin(ULONGLONG now) {
    if (!due_ || now < *due_) return false;
    due_.reset(); ++attempts_; return true;
  }
  void Healthy(ULONGLONG now) {
    due_.reset();
    if (!healthy_since_) healthy_since_ = now;
    if (now - *healthy_since_ >= 60000) Reset();
  }
  void Reset() { attempts_ = 0; due_.reset(); healthy_since_.reset(); }
  [[nodiscard]] bool Pending() const { return due_.has_value(); }
  [[nodiscard]] std::optional<ULONGLONG> Due() const { return due_; }
  [[nodiscard]] int Attempts() const { return attempts_; }
 private:
  int attempts_ = 0;
  std::optional<ULONGLONG> due_, healthy_since_;
};

bool NeedsUserAction(const std::string& e) {
  for (const char* phrase : {"Windows Volume Control", "confirm", "restoration", "source app is no longer running",
                             "reopen the source app", "close the CleanWave preview"}) {
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; };
    if (lower(e).find(lower(phrase)) != std::string::npos) return true;
  }
  return false;
}

std::wstring ModeName(int mode, bool arabic) {
  switch (mode) {
    case 1: return arabic ? L"مرشّح الموسيقى" : L"Music Filter";
    case 2: return arabic ? L"محسّن" : L"Optimized";
    case 3: return arabic ? L"صارم" : L"Strict";
    default: return arabic ? L"متوقف" : L"Off";
  }
}

struct CommandResult { Engine::Selection selection; bool success = false, automatic_output = false; std::string error; };
struct CatalogResult { std::string state, apps; bool includes_apps = false; std::vector<SourceAppChoice> windows; };

class Host {
 public:
  explicit Host(HINSTANCE instance) : instance_(instance), popup_(instance, PopupActions()) {}

  int Run(bool open_window) {
    locations_ = Locations::Discover();
    preferences_ = Preferences::Load(locations_.Preferences());
    settings_revision_ = preferences_.settings_version;
    PublishSettings();
    open_on_ready_ = open_window;
    show_message_ = RegisterWindowMessageW(L"Sukut.ShowWindow");
    host_exit_message_ = RegisterWindowMessageW(L"Sukut.HostExit");
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Procedure;
    wc.hInstance = instance_;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives the
    // TaskbarCreated broadcast and tray callbacks.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Sukut", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) return 1;
    AddTrayIcon();

    // Load and start the engine on its own thread; the tray stays responsive.
    const auto dll = locations_.app_directory / L"SukutEngine.dll";
    const auto data = locations_.data_root, resources = locations_.resource_root;
    const int amplification = preferences_.amplification_db;
    engine_.Post([this, dll, data, resources, amplification] {
      auto error = std::make_unique<std::string>();
      bool ok = engine_.Load(dll, *error) && engine_.Create(data, resources, *error);
      if (ok) engine_.WatchDevices(window_, WM_DEVICE_NOTICE, &command_);
      std::string ignored;
      if (ok) engine_.SetAmplification(amplification, ignored);
      PostMessageW(window_, WM_ENGINE_READY, ok ? 1 : 0, reinterpret_cast<LPARAM>(error.release()));
    });
    pipe_thread_ = std::thread([this] { ServePipe(); });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      if (popup_.IsOpen() && IsDialogMessageW(popup_.Window(), &msg)) continue;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    return 0;
  }

 private:
  TrayPopup::Actions PopupActions() {
    return {[this](int mode) { SelectFromTray(mode); }, [this] { OpenWindow(); },
      [this](int value) { ChangeTraySetting(0, std::to_string(value)); },
      [this](const std::string& id) { ChangeTraySetting(1, id); },
      [this](const std::string& pid) { ChangeTraySetting(2, pid); },
      [this] { TrimMemory(); }, [this] { RequestCatalog(); },
      [this](int db) { ChangeTraySetting(3,std::to_string(db)); }};
  }

  // Return pages the tray no longer touches (flyout graphics, the window's
  // pipe traffic) to Windows. Only while Off: a running filter must never
  // take page faults on its audio thread.
  void TrimMemory() {
    if (mode_ != 0 || command_ || exiting_) return;
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
  }

  // The window closed to the tray: say where Sukut went and why it is cheaper.
  void ShowTrayNotice() {
    // Shown the first time only; a marker beside the preferences remembers it.
    const auto marker = locations_.Preferences().parent_path() / L"tray-notice-shown";
    std::error_code exists_error;
    if (std::filesystem::exists(marker, exists_error)) return;
    { std::ofstream(marker) << "1\n"; }
    const bool ar = preferences_.Arabic();
    NOTIFYICONDATAW notice{sizeof(notice)};
    notice.hWnd = window_; notice.uID = 1; notice.uFlags = NIF_INFO;
    notice.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    // LoadImageW, not LoadIconMetric: the host runs without Common Controls v6.
    notice.hBalloonIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wcsncpy_s(notice.szInfoTitle, ar ? L"سُكوت في منطقة الإشعارات" : L"Sukut is in the tray", _TRUNCATE);
    wcsncpy_s(notice.szInfo, mode_ == 0
        ? (ar ? L"أُغلقت النافذة لتوفير الذاكرة. يستهلك سُكوت في منطقة الإشعارات أقل من 20 ميغابايت. اضغط أيقونة س لفتحه."
              : L"The window closed to save memory. In the tray Sukut uses under 20 MB. Click the س icon to open it.")
        : (ar ? L"يستمر الترشيح. أُغلقت النافذة لتوفير الذاكرة؛ تحتاج منطقة الإشعارات أقل من 20 ميغابايت إضافة إلى مرشّح الصوت."
              : L"Filtering continues. The window closed to save memory; the tray needs under 20 MB plus the audio filter."),
        _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notice);
    if (notice.hBalloonIcon) DestroyIcon(notice.hBalloonIcon);
  }

  void ReloadPreferences() {
    auto fresh = Preferences::Load(locations_.Preferences());
    // Routing belongs to the host once applied. An older deferred window save
    // must not undo a tray or Windows-default change before its next poll.
    if (fresh.settings_version < settings_revision_) {
      fresh.output = preferences_.output; fresh.source = preferences_.source; fresh.capture = preferences_.capture;
      fresh.buffering = preferences_.buffering; fresh.source_pid = preferences_.source_pid;
      fresh.preferred_mode = preferences_.preferred_mode;
      fresh.optimized_background_db = preferences_.optimized_background_db;
      fresh.optimized_mix = preferences_.optimized_mix;
      fresh.settings_version = settings_revision_;
    }
    preferences_ = std::move(fresh);
  }

  void PublishSettings() {
    preferences_.settings_version = settings_revision_;
    std::lock_guard lock(settings_mutex_);
    settings_snapshot_ = ",\"hostSettingsVersion\":" + std::to_string(settings_revision_) + ",\"hostOutput\":\"" + json::Escape(preferences_.output)
      + "\",\"hostBuffering\":" + std::to_string(preferences_.buffering) + ",\"hostSourcePid\":" + std::to_string(preferences_.source_pid)
      + ",\"hostOptimizedBackgroundDb\":" + std::to_string(preferences_.optimized_background_db)
      + ",\"hostAdvancedOptimized\":" + (preferences_.optimized_mix.advanced ? "true" : "false")
      + ",\"hostAdvancedVoiceDb\":" + std::to_string(preferences_.optimized_mix.voice_db)
      + ",\"hostAdvancedMusicDb\":" + std::to_string(preferences_.optimized_mix.music_db)
      + ",\"hostAdvancedBackgroundDb\":" + std::to_string(preferences_.optimized_mix.background_db);
  }

  void RequestCatalog(bool include_apps = true) {
    if (!engine_ready_ || exiting_) return;
    if (catalog_pending_) { catalog_apps_pending_ |= include_apps; return; }
    catalog_pending_ = true;
    engine_.Post([this, include_apps] {
      auto result = std::make_unique<CatalogResult>(); std::string error;
      engine_.Refresh();
      if (include_apps) { result->includes_apps = true; result->apps = engine_.Apps(error); result->windows = VisibleSourceApps(); }
      result->state = engine_.Read(-1, error);
      PostMessageW(window_, WM_CATALOG, 0, reinterpret_cast<LPARAM>(result.release()));
    });
  }

  void ApplyPendingOutput() {
    if (!output_changes_.Pending() || command_ || !engine_ready_ || exiting_ || reading_) return;
    const auto resolved = output_changes_.Resolve(status_.endpoints);
    if (!resolved) { if (output_changes_.Pending()) SetTimer(window_, kDeviceTimer,250,nullptr); return; }
    ReloadPreferences();
    const auto& output = *resolved;
    // Keep Windows' chosen target even if starting its audio client fails.
    // Recovery must retry the headphones, not silently return to old speakers.
    preferences_.output = output; preferences_.settings_version = ++settings_revision_;
    preferences_.Save(locations_.Preferences()); UpdateTray();
    policy_.Reset(); KillTimer(window_, kRestartTimer);
    // Re-establish the route even if the same endpoint is selected: Windows
    // may have made it the default and bypassed CABLE. The restart's explicit
    // Off handoff also records this new physical device as the restore target.
    // While Off this selects the device without starting filtering.
    RunSelect(mode_, mode_ != 0, output, true);
  }

  void ShowOutputNotice(const std::string& output, bool success) {
    if (success && output == last_notified_output_ && GetTickCount64() - output_notice_at_ < 5000) return;
    const auto endpoint = std::find_if(status_.endpoints.begin(), status_.endpoints.end(), [&](const auto& e) { return e.id == output; });
    const auto name = endpoint == status_.endpoints.end() ? Widen(output) : Widen(endpoint->name);
    const bool ar = preferences_.Arabic();
    NOTIFYICONDATAW notice{sizeof(notice)};
    notice.hWnd = window_; notice.uID = 1; notice.uFlags = NIF_INFO;
    notice.dwInfoFlags = (success ? NIIF_INFO : NIIF_WARNING) | NIIF_NOSOUND;
    wcsncpy_s(notice.szInfoTitle, success ? (ar ? L"تم تبديل مخرج الصوت" : L"Audio output switched")
        : (ar ? L"تعذّر تبديل مخرج الصوت" : L"Couldn't switch audio output"), _TRUNCATE);
    const auto text = success ? (ar ? L"يستخدم سُكوت الآن " : L"Sukut now uses ") + name
        : name + (ar ? L". افتح سُكوت لإعادة المحاولة." : L". Open Sukut to retry.");
    wcsncpy_s(notice.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notice);
    if (success) { last_notified_output_ = output; output_notice_at_ = GetTickCount64(); }
  }

  void ChangeTraySetting(int field, const std::string& value) {
    if (command_ || exiting_ || !engine_ready_) return;
    ReloadPreferences();
    auto s = Selection(mode_, false);
    if (field == 0) s.buffering = std::clamp(std::atoi(value.c_str()), 0, 2);
    else if (field == 1) {
      if (std::none_of(status_.endpoints.begin(), status_.endpoints.end(), [&](const auto& e) { return e.id == value && e.flow == 1 && !e.is_virtual; })) return;
      s.output = Widen(value);
    } else if(field==2) s.pid = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else s.optimized_background_db=std::clamp(std::atoi(value.c_str()),-20,10);
    if (mode_ != 0 && !SetupReady()) { popup_.Close(); OpenWindow(); return; }
    if (mode_ == 0 && field != 1) {
      preferences_.buffering = s.buffering; preferences_.source_pid = s.pid;
      preferences_.optimized_background_db=s.optimized_background_db;
      preferences_.optimized_mix=s.optimized_mix;
      preferences_.settings_version = ++settings_revision_; preferences_.Save(locations_.Preferences()); UpdateTray(); return;
    }
    if(field==3) {
      preferences_.optimized_background_db=s.optimized_background_db;
      preferences_.optimized_mix=s.optimized_mix;
      preferences_.settings_version=++settings_revision_;preferences_.Save(locations_.Preferences());
    }
    BeginSelection(s);
  }

  void ShowContextMenu(POINT at) {
    popup_.Close(); ReloadPreferences();
    if (at.x == -1 && at.y == -1) GetCursorPos(&at);
    ThemeNativeMenu(window_,UseDarkTheme(preferences_));
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, preferences_.Arabic() ? L"فتح سُكوت" : L"Open Sukut");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, preferences_.Arabic() ? L"خروج" : L"Exit");
    SetForegroundWindow(window_);
    const UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | (preferences_.Arabic() ? TPM_LAYOUTRTL : 0), at.x, at.y, window_, nullptr);
    DestroyMenu(menu); PostMessageW(window_, WM_NULL,0,0);
    if (selected == 1) OpenWindow(); else if (selected == 2) BeginExit(false);
  }

  // ----- tray icon ---------------------------------------------------------
  void LoadIcons() {
    if(mode_icon_){DestroyIcon(mode_icon_);mode_icon_=nullptr;} icon_mode_=-1;
    for (HICON& icon : icons_) if (icon) { DestroyIcon(icon); icon = nullptr; }
    const UINT dpi = GetDpiForWindow(window_) ? GetDpiForWindow(window_) : 96;
    const int size = std::max(16, GetSystemMetricsForDpi(SM_CXSMICON, dpi));
    const wchar_t* files[] = {L"Tray-on-dark.ico", L"Tray-off-dark.ico", L"Tray-on-light.ico", L"Tray-off-light.ico"};
    for (int i = 0; i < 4; ++i)
      icons_[i] = static_cast<HICON>(LoadImageW(nullptr, (locations_.app_directory / L"Assets" / files[i]).c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
    // The taskbar follows Windows mode, independently of Sukut's own theme.
    DWORD light = 0, bytes = sizeof(light);
    dark_taskbar_ = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) != ERROR_SUCCESS || light == 0;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    high_contrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
  }
  HICON CurrentIcon() {
    const int mode=mode_.load();
    if(active_ && !high_contrast_ && mode!=0) {
      if(!mode_icon_ || icon_mode_!=mode) {
        if(mode_icon_)DestroyIcon(mode_icon_);
        mode_icon_=TintIcon(icons_[dark_taskbar_?0:2],ModeColor(mode,dark_taskbar_));icon_mode_=mode;
      }
      if(mode_icon_)return mode_icon_;
    }
    HICON icon = icons_[(dark_taskbar_ ? 0 : 2) + (active_ && !high_contrast_ ? 0 : 1)];
    return icon ? icon : LoadIconW(instance_, MAKEINTRESOURCEW(1));
  }
  void AddTrayIcon() {
    LoadIcons();
    tray_ = NOTIFYICONDATAW{sizeof(tray_)};
    tray_.hWnd = window_;
    tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    tray_.uCallbackMessage = WM_TRAY;
    tray_.hIcon = CurrentIcon();
    wcscpy_s(tray_.szTip, L"سُكوت Sukut");
    Shell_NotifyIconW(NIM_ADD, &tray_);
    tray_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
  }
  void UpdateTray() {
    PublishSettings();
    const bool active = mode_ != 0 && status_.state == 1;
    const std::wstring tip = L"سُكوت Sukut · " + StatusText();
    if (active != active_ || tip != tray_tip_ || (active && icon_mode_ != mode_.load())) {
      active_ = active; tray_tip_ = tip;
      tray_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
      tray_.hIcon = CurrentIcon();
      wcsncpy_s(tray_.szTip, tip.c_str(), _TRUNCATE);
      Shell_NotifyIconW(NIM_MODIFY, &tray_);
    }
    if (popup_.IsOpen()) popup_.Update(PopupModel());
  }

  std::wstring StatusText() const {
    const bool ar = preferences_.Arabic();
    if (!engine_ready_) return ar ? L"جارٍ التجهيز" : L"Preparing";
    if (command_) return ar ? L"جارٍ التبديل…" : L"Switching…";
    if (policy_.Pending()) return ar ? L"إعادة الاتصال" : L"Reconnecting";
    if (!attention_.empty()) return ar ? L"تنبيه · افتح سُكوت" : L"Attention · open Sukut";
    if (mode_ == 0) return ar ? L"متوقف" : L"Off";
    if (status_.state == 1) {
      if (status_.standby) return ar ? L"استعداد · لا يوجد صوت" : L"Standby · nothing playing";
      if (status_.paused) return ar ? L"جارٍ الاستعادة" : L"Recovering";
      return (ar ? L"نشط · " : L"Active · ") + ModeName(mode_, ar);
    }
    return ar ? L"جارٍ التجهيز" : L"Preparing";
  }

  TrayPopup::Model PopupModel() const {
    TrayPopup::Model m;
    m.arabic = preferences_.Arabic();
    m.dark = UseDarkTheme(preferences_);
    m.busy = command_ || !engine_ready_;
    m.mode = mode_;
    m.preferred = std::clamp(preferences_.preferred_mode, 1, 3);
    m.status = StatusText();
    m.attention = !attention_.empty();
    m.standby = mode_ != 0 && status_.state == 1 && status_.standby;
    m.buffering = preferences_.buffering; m.output = preferences_.output; m.source_pid = std::to_string(preferences_.source_pid);
    m.optimized_background_db=preferences_.optimized_background_db;
    m.advanced_optimized=preferences_.optimized_mix.advanced;
    for (const auto& e : status_.endpoints) if (e.flow == 1 && !e.is_virtual) m.outputs.push_back({e.id, Widen(e.name)});
    m.apps.push_back({"0", m.arabic ? L"صوت النظام بالكامل" : L"All system audio"});
    for (const auto& a : source_apps_) m.apps.push_back(a);
    return m;
  }

  void TogglePopup(POINT at) {
    if (popup_.IsOpen()) { popup_.Close(); return; }
    // The click that dismissed the flyout must not reopen it.
    if (GetTickCount64() - popup_.ClosedAt() < 300) return;
    ReloadPreferences();
    NOTIFYICONIDENTIFIER id{sizeof(id)};
    id.hWnd = window_; id.uID = 1;
    RECT anchor{at.x, at.y, at.x + 1, at.y + 1};
    RECT icon{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &icon))) anchor = icon;
    popup_.Show(anchor, PopupModel());
    RequestCatalog();
    RequestRead();
    SetTimer(window_, kPopupTimer, 1000, nullptr);
  }

  // ----- engine status and supervision ------------------------------------
  void RequestRead(bool full = false) {
    if (!engine_ready_ || reading_) return;
    reading_ = true;
    const std::int64_t version = full ? -1 : status_.endpoints_version;
    engine_.Post([this, version] {
      std::string error;
      auto text = std::make_unique<std::string>(engine_.Read(version, error));
      PostMessageW(window_, WM_ENGINE_STATE, 0, reinterpret_cast<LPARAM>(text.release()));
    });
  }

  void OnState(std::unique_ptr<std::string> text) {
    reading_ = false;
    if (!text->empty()) status_ = EngineStatus::Parse(*text, status_);
    ApplyPendingOutput();
    if (!client_ && !command_ && mode_ != 0 && !exiting_) Supervise();
    UpdateTray();
  }

  bool SetupReady() {
    ReloadPreferences();
    if (!preferences_.setup_completed) return false;
    const auto has = [&](const std::string& id, int flow, bool is_virtual) {
      return std::any_of(status_.endpoints.begin(), status_.endpoints.end(),
                         [&](const auto& e) { return e.id == id && e.flow == flow && e.is_virtual == is_virtual; });
    };
    return has(preferences_.source, 1, true) && has(preferences_.capture, 0, true) && has(preferences_.output, 1, false);
  }

  // The same decisions the window makes while it supervises (ShellView.Poll /
  // AudioFault), applied only while no window is connected.
  void Supervise() {
    const ULONGLONG now = GetTickCount64();
    if (status_.error == "selected source app is no longer running" && !status_.busy && status_.state == 0 && !status_.pipeline_active) {
      // Closing the chosen source ends that session; never silently start
      // filtering other apps. The next explicit mode uses all system audio.
      policy_.Reset();
      preferences_ = Preferences::Load(locations_.Preferences());
      preferences_.source_pid = 0;
      preferences_.Save(locations_.Preferences());
      attention_ = preferences_.Arabic() ? L"أُغلق التطبيق المصدر." : L"Source app closed.";
      RunSelect(0, false);
      return;
    }
    std::string fault = status_.error;
    if (fault.empty() && !status_.busy && (status_.state != 1 || !status_.pipeline_active || status_.controller_age_ms > 5000))
      fault = "Audio stopped responding.";
    if (!fault.empty()) {
      if (NeedsUserAction(fault) || !SetupReady()) { policy_.Reset(); attention_ = Widen(fault); return; }
      if (policy_.Request(now)) {
        KillTimer(window_, kRestartTimer);
        SetTimer(window_, kRestartTimer, static_cast<UINT>(std::max<ULONGLONG>(1, *policy_.Due() - now)), nullptr);
      } else {
        attention_ = Widen(fault);
      }
    } else if (status_.state == 1 && status_.pipeline_active && !status_.busy && !status_.paused) {
      policy_.Healthy(now);
      attention_.clear();
    }
  }

  void OnRestartTimer() {
    KillTimer(window_, kRestartTimer);
    if (exiting_ || client_ || mode_ == 0) { policy_.Reset(); return; }
    if (command_ || reading_) { SetTimer(window_, kRestartTimer, 200, nullptr); return; }
    if (policy_.TryBegin(GetTickCount64())) RunSelect(mode_, true);
  }

  Engine::Selection Selection(int mode, bool restart) const {
    Engine::Selection s;
    s.mode = mode; s.restart = restart;
    s.source = Widen(preferences_.source); s.capture = Widen(preferences_.capture); s.output = Widen(preferences_.output);
    s.pid = preferences_.source_pid; s.buffering = preferences_.buffering;
    s.cable_confirmed = preferences_.cable_confirmed; s.optimized_background_db = preferences_.optimized_background_db; s.optimized_mix=preferences_.optimized_mix;
    return s;
  }
  void RunSelect(int mode, bool restart, const std::string& output = {}, bool automatic_output = false) {
    ReloadPreferences();
    auto s = Selection(mode, restart);
    if (!output.empty()) s.output = Widen(output);
    BeginSelection(s, automatic_output);
  }
  void BeginSelection(const Engine::Selection& s, bool automatic_output = false) {
    command_ = true;
    UpdateTray();
    engine_.Post([this, s, automatic_output] {
      auto result = std::make_unique<CommandResult>();
      result->selection = s;
      result->automatic_output = automatic_output;
      result->success = engine_.Select(s, result->error);
      PostMessageW(window_, WM_COMMAND_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    });
  }

  void OnCommandDone(std::unique_ptr<CommandResult> result) {
    command_ = false;
    if (result->success) {
      const auto& s = result->selection;
      mode_ = s.mode;
      ReloadPreferences();
      if(s.mode!=0)preferences_.preferred_mode=s.mode;
      preferences_.source = Narrow(s.source); preferences_.capture = Narrow(s.capture); preferences_.output = Narrow(s.output);
      preferences_.buffering = s.buffering; preferences_.source_pid = s.pid;
      preferences_.optimized_background_db=s.optimized_background_db;
      preferences_.optimized_mix=s.optimized_mix;
      preferences_.settings_version = ++settings_revision_; preferences_.Save(locations_.Preferences()); attention_.clear();
      if (s.mode == 0) { policy_.Reset(); if (!client_) TrimMemory(); }
    } else if (!exiting_) {
      attention_ = Widen(result->error);
    }
    if (result->automatic_output && !exiting_) ShowOutputNotice(Narrow(result->selection.output), result->success);
    RequestRead();
    UpdateTray();
  }

  void SelectFromTray(int mode) {
    if (command_ || exiting_ || !engine_ready_) return;
    if (mode == mode_ && attention_.empty()) return;
    if (mode != 0 && !SetupReady()) { popup_.Close(); OpenWindow(); return; }
    attention_.clear();
    policy_.Reset();
    KillTimer(window_, kRestartTimer);
    RunSelect(mode, false);
  }

  // ----- the window process -----------------------------------------------
  void OpenWindow() {
    if (exiting_) return;
    if (client_ && client_window_) {
      AllowSetForegroundWindow(client_pid_);
      PostMessageW(client_window_, show_message_, 0, 0);
      return;
    }
    if (GetTickCount64() - launched_at_ < 15000) return;  // already starting
    const auto exe = locations_.WindowExecutable();
    std::wstring command = L"\"" + exe.wstring() + L"\" --from-host";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       locations_.app_directory.c_str(), &si, &pi)) {
      AllowSetForegroundWindow(pi.dwProcessId);
      CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
      launched_at_ = GetTickCount64();
    } else {
      MessageBoxW(nullptr, L"Sukut could not open its window. Keep the app folder beside Sukut.exe.", L"سُكوت Sukut", MB_OK | MB_ICONERROR);
    }
  }

  // ----- named pipe for the window ----------------------------------------
  static std::wstring PipeName() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"\\\\.\\pipe\\Sukut.Host." + std::to_wstring(session);
  }

  void ServePipe() {
    bool first = true;
    while (!stopping_) {
      HANDLE pipe = CreateNamedPipeW(PipeName().c_str(), PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, 1 << 20, 1 << 20, 0, nullptr);
      first = false;
      if (pipe == INVALID_HANDLE_VALUE) return;
      const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
      if (!connected || stopping_) { CloseHandle(pipe); continue; }
      client_ = true;
      PostMessageW(window_, WM_CLIENT_CHANGED, 1, 0);
      std::string request;
      while (!stopping_ && ReadMessage(pipe, request)) {
        const std::string reply = HandleRequest(request);
        DWORD written = 0;
        if (!WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr)) break;
      }
      client_ = false;
      client_window_ = nullptr;
      client_pid_ = 0;
      FlushFileBuffers(pipe);
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      PostMessageW(window_, WM_CLIENT_CHANGED, 0, 0);
    }
  }

  static bool ReadMessage(HANDLE pipe, std::string& out) {
    out.clear();
    char chunk[16384];
    while (true) {
      DWORD read = 0;
      if (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr)) { out.append(chunk, read); return true; }
      if (GetLastError() != ERROR_MORE_DATA) return false;
      out.append(chunk, read);
    }
  }

  static std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
      const auto at = text.find('\x1f', start);
      parts.push_back(text.substr(start, at == std::string::npos ? std::string::npos : at - start));
      if (at == std::string::npos) return parts;
      start = at + 1;
    }
  }

  // Runs on the pipe thread; engine work still runs on the engine thread.
  std::string HandleRequest(const std::string& request) {
    const auto f = Split(request);
    const auto& verb = f[0];
    const auto arg = [&](std::size_t i) { return i < f.size() ? f[i] : std::string(); };
    const auto number = [&](std::size_t i) { return std::atoll(arg(i).c_str()); };
    const auto ok = [](const std::string& payload) { return "1" + payload; };
    const auto fail = [](const std::string& error) { return "0" + error; };
    if (verb == "hello") {
      client_window_ = reinterpret_cast<HWND>(static_cast<INT_PTR>(number(1)));
      client_pid_ = static_cast<DWORD>(number(2));
      const bool ready = engine_.Run<bool>([this] { return engine_.Ready(); });
      return ready ? ok("{\"host\":1}") : fail(engine_error_.empty() ? "Sukut audio engine could not start." : engine_error_);
    }
    if (verb == "read") {
      std::string error;
      const std::int64_t version = number(1);
      std::string text = engine_.Run<std::string>([&] { return engine_.Read(version, error); });
      if (text.empty()) return fail(error);
      // The window adopts mode changes made from the tray flyout.
      const auto close = text.rfind('}');
      std::string settings;
      { std::lock_guard lock(settings_mutex_); settings = settings_snapshot_; }
      settings += ",\"mode\":" + std::to_string(mode_.load()) + ",\"hostCommand\":" + (command_ ? "1" : "0");
      if (close != std::string::npos) text.insert(close, settings);
      return ok(text);
    }
    if (verb == "apps") {
      std::string error;
      const std::string text = engine_.Run<std::string>([&] { return engine_.Apps(error); });
      return text.empty() && !error.empty() ? fail(error) : ok(text);
    }
    if (verb == "select") {
      if (command_.exchange(true)) return fail("audio route is still switching");
      Engine::Selection s;
      s.mode = std::clamp(static_cast<int>(number(1)), 0, 3);
      s.source = Widen(arg(2)); s.capture = Widen(arg(3)); s.output = Widen(arg(4));
      s.pid = static_cast<std::uint32_t>(number(5)); s.buffering = static_cast<int>(number(6));
      s.cable_confirmed = number(7) != 0; s.optimized_background_db = static_cast<int>(number(8)); s.restart = number(9) != 0;
      if(f.size()>=14) { s.optimized_mix.advanced=number(10)!=0; s.optimized_mix.voice_db=std::clamp(static_cast<int>(number(11)),-20,10);
        s.optimized_mix.music_db=std::clamp(static_cast<int>(number(12)),-20,10); s.optimized_mix.background_db=std::clamp(static_cast<int>(number(13)),-20,10); }
      std::string error;
      const bool success = engine_.Run<bool>([&] { return engine_.Select(s, error); });
      auto result = std::make_unique<CommandResult>();
      result->selection = s; result->success = success; result->error = error;
      SendMessageW(window_, WM_COMMAND_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
      if (!success) return fail(error);
      return ok("");
    }
    if (verb == "refresh") { engine_.Run<bool>([this] { engine_.Refresh(); return true; }); return ok(""); }
    if (verb == "amp") {
      std::string error;
      const int db = static_cast<int>(number(1));
      return engine_.Run<bool>([&] { return engine_.SetAmplification(db, error); }) ? ok("") : fail(error);
    }
    if (verb == "exit") { PostMessageW(window_, WM_EXIT_REQUEST, 1, 0); return ok(""); }
    return fail("unknown request");
  }

  // ----- exit --------------------------------------------------------------
  void BeginExit(bool from_window) {
    if (exiting_) return;
    exiting_ = true;
    popup_.Close();
    KillTimer(window_, kSuperviseTimer); KillTimer(window_, kRestartTimer); KillTimer(window_, kPopupTimer);
    // Ask an open window to save its settings and close first.
    if (client_ && client_window_ && !from_window) PostMessageW(client_window_, host_exit_message_, 0, 0);
    exit_started_ = GetTickCount64();
    SetTimer(window_, kExitTimer, 100, nullptr);
  }
  void OnExitTimer() {
    if (client_ && GetTickCount64() - exit_started_ < 4000) return;
    KillTimer(window_, kExitTimer);
    ReloadPreferences(); preferences_.settings_version = settings_revision_; preferences_.Save(locations_.Preferences());
    // Engine teardown restores the original audio routes. If a driver refuses
    // cancellation, leave it to the independent journal guard.
    SetTimer(window_, kForceExitTimer, 12000, nullptr);
    engine_.Post([this] { engine_.Destroy(); PostMessageW(window_, WM_ENGINE_DESTROYED, 0, 0); });
  }
  void FinishExit() {
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    stopping_ = true;
    if (pipe_thread_.joinable()) {
      CancelSynchronousIo(pipe_thread_.native_handle());
      // Unblock a pending ConnectNamedPipe by connecting once.
      HANDLE poke = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (poke != INVALID_HANDLE_VALUE) CloseHandle(poke);
      pipe_thread_.join();
    }
    DestroyWindow(window_);
    PostQuitMessage(0);
  }

  // ----- window procedure ---------------------------------------------------
  static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
      return DefWindowProcW(hwnd, message, w, l);
    }
    auto* self = reinterpret_cast<Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Handle(hwnd, message, w, l) : DefWindowProcW(hwnd, message, w, l);
  }

  LRESULT Handle(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == taskbar_created_) { AddTrayIcon(); UpdateTray(); return 0; }
    switch (message) {
      case WM_TRAY: {
        const UINT event = LOWORD(l);
        const POINT at{GET_X_LPARAM(w), GET_Y_LPARAM(w)};
        // While the window is open it is the one place to control Sukut:
        // the tray click brings it forward instead of opening the flyout.
        if (event == NIN_SELECT || event == NIN_KEYSELECT) { if (client_ && client_window_) OpenWindow(); else TogglePopup(at); }
        else if (event == WM_CONTEXTMENU) ShowContextMenu(at);
        else if (event == WM_LBUTTONDBLCLK) { popup_.Close(); OpenWindow(); }
        return 0;
      }
      case WM_OPEN_REQUEST: OpenWindow(); return 0;
      case WM_READ_REQUEST: RequestRead(); return 0;
      case WM_DEVICE_NOTICE: {
        std::unique_ptr<DeviceNotice> notice(reinterpret_cast<DeviceNotice*>(l));
        if (exiting_) return 0;
        if (notice->default_output) output_changes_.Observe(Narrow(notice->id), notice->internal, preferences_.source);
        if (notice->arrived) output_changes_.ObserveArrival(Narrow(notice->id), preferences_.source);
        SetTimer(window_, kDeviceTimer, 180, nullptr);
        return 0;
      }
      case WM_CATALOG: {
        std::unique_ptr<CatalogResult> result(reinterpret_cast<CatalogResult*>(l)); catalog_pending_ = false;
        if (!result->state.empty()) status_ = EngineStatus::Parse(result->state, status_);
        if (result->includes_apps) {
          std::vector<SourceAppChoice> sessions;
          if (const auto apps = json::Parse(result->apps); apps && apps->kind == json::Value::Kind::Array)
            for (const auto& a : apps->items) sessions.push_back({static_cast<std::uint32_t>(a.Num("pid")), Widen(a.Str("name"))});
          source_apps_.clear();
          for (auto& a : MergeSourceApps(sessions, result->windows)) source_apps_.push_back({std::to_string(a.pid), std::move(a.name)});
        }
        ApplyPendingOutput(); UpdateTray();
        if (catalog_apps_pending_) { catalog_apps_pending_ = false; RequestCatalog(); }
        return 0;
      }
      case WM_ENGINE_READY: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(l));
        engine_ready_ = w != 0;
        if (!engine_ready_) {
          engine_error_ = *error;
          MessageBoxW(nullptr, Widen(*error).c_str(), L"سُكوت Sukut", MB_OK | MB_ICONERROR);
          BeginExit(false);
          return 0;
        }
        RequestRead(true);
        SetTimer(window_, kSuperviseTimer, 2500, nullptr);
        if (open_on_ready_) OpenWindow();
        return 0;
      }
      case WM_ENGINE_STATE: OnState(std::unique_ptr<std::string>(reinterpret_cast<std::string*>(l))); return 0;
      case WM_COMMAND_DONE: OnCommandDone(std::unique_ptr<CommandResult>(reinterpret_cast<CommandResult*>(l))); return 0;
      case WM_CLIENT_CHANGED:
        if (w == 0) {  // the host supervises again while no window is connected
          ReloadPreferences(); preferences_.settings_version = settings_revision_; preferences_.Save(locations_.Preferences());
          launched_at_ = 0; attention_.clear(); policy_.Reset(); RequestRead();
          // An exit request arrives before the pipe closes, so exiting_ is already set then.
          if (!exiting_) { ShowTrayNotice(); TrimMemory(); }
        } else {
          popup_.Close();  // the window takes over; no flyout beside it
        }
        return 0;
      case WM_EXIT_REQUEST: BeginExit(w != 0); return 0;
      case WM_ENGINE_DESTROYED: KillTimer(window_, kForceExitTimer); FinishExit(); return 0;
      case WM_TIMER:
        if (w == kSuperviseTimer) { if (!client_ && mode_ != 0 && !command_) RequestRead(); }
        else if (w == kRestartTimer) OnRestartTimer();
        else if (w == kPopupTimer) { if (popup_.IsOpen()) RequestRead(); else KillTimer(window_, kPopupTimer); }
        else if (w == kDeviceTimer) { KillTimer(window_, kDeviceTimer); RequestCatalog(false); RequestRead(true); ApplyPendingOutput(); }
        else if (w == kExitTimer) OnExitTimer();
        else if (w == kForceExitTimer) { Shell_NotifyIconW(NIM_DELETE, &tray_); ExitProcess(0); }
        return 0;
      case WM_SETTINGCHANGE:
      case WM_THEMECHANGED:
      case WM_DISPLAYCHANGE:
        LoadIcons();
        active_ = !active_;  // force the icon to be re-sent
        UpdateTray();
        return 0;
      // The hidden host window must never be destroyed by a stray close;
      // Sukut exits only through Exit (tray, window) or Windows sign-out.
      case WM_CLOSE: return 0;
      case WM_QUERYENDSESSION: return TRUE;
      case WM_ENDSESSION:
        // Restore audio routes before Windows ends the session.
        if (w) engine_.Run<bool>([this] { engine_.Destroy(); return true; });
        return 0;
      default: break;
    }
    return DefWindowProcW(hwnd, message, w, l);
  }

  HINSTANCE instance_;
  HWND window_ = nullptr;
  Locations locations_;
  Preferences preferences_;
  Engine engine_;
  TrayPopup popup_;
  NOTIFYICONDATAW tray_{};
  HICON icons_[4]{};
  HICON mode_icon_=nullptr;
  int icon_mode_=-1;
  bool dark_taskbar_ = true, high_contrast_ = false, active_ = false;
  std::wstring tray_tip_;
  EngineStatus status_;
  RestartPolicy policy_;
  std::wstring attention_;
  std::string engine_error_;
  std::atomic<int> mode_{0};
  std::atomic<bool> command_{false}, client_{false}, stopping_{false};
  std::atomic<HWND> client_window_{nullptr};
  std::atomic<DWORD> client_pid_{0};
  bool engine_ready_ = false, reading_ = false, exiting_ = false, open_on_ready_ = true;
  ULONGLONG launched_at_ = 0, exit_started_ = 0;
  UINT show_message_ = 0, host_exit_message_ = 0, taskbar_created_ = 0;
  std::thread pipe_thread_;
  std::vector<TrayPopup::Choice> source_apps_;
  DefaultOutputChanges output_changes_;
  std::string last_notified_output_;
  ULONGLONG output_notice_at_ = 0;
  std::uint64_t settings_revision_ = 0;
  bool catalog_pending_ = false, catalog_apps_pending_ = false;
  std::mutex settings_mutex_;
  std::string settings_snapshot_;
};

}  // namespace
}  // namespace sukut::host

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const std::wstring args = command_line ? command_line : L"";
  const bool open_window = args.find(L"--no-window") == std::wstring::npos && args.find(L"--tray") == std::wstring::npos;
  HANDLE instance_lock = CreateMutexW(nullptr, FALSE, L"Local\\SukutHost");
  if (instance_lock && GetLastError() == ERROR_ALREADY_EXISTS) {
    // Already running: just bring the window up (unless the window started us).
    if (open_window) {
      if (HWND host = FindWindowW(L"SukutHostWindow", nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(host, &pid);
        AllowSetForegroundWindow(pid);
        PostMessageW(host, WM_APP + 2, 0, 0);
      }
    }
    CloseHandle(instance_lock);
    return 0;
  }
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);
  const int result = sukut::host::Host(instance).Run(open_window);
  if (instance_lock) CloseHandle(instance_lock);
  return result;
}
