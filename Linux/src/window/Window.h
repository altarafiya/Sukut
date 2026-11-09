#pragma once

// Sukut's window on Linux (Linux/PLAN.md section 7): GNOME-native, following
// the idea of Windows beta.14. A sidebar (Audio, Settings), the Audio page with
// the Playback card beside the modes, Settings with About last, the mode help
// dialog and the first-run setup. All state comes from sukut-host; the window
// only asks it to change things.
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <adwaita.h>

#include "window/HostLink.h"
#include "window/Model.h"
#include "window/PhoneTools.h"

namespace sukut::window {

class SukutWindow {
 public:
  enum class Page { Audio, Phone, Settings };
  // The Phone page's flows, as on Windows (PhoneSetup.cs).
  enum class PhoneTask { Choices, FullSetup, UsbHelp, Activate, Install };
  enum class Severity { None, Info, Success, Warning };

  // link == nullptr: offline (the --capture test), the model is set with Apply.
  SukutWindow(AdwApplication* app, HostLink* link);
  SukutWindow(const SukutWindow&) = delete;
  SukutWindow& operator=(const SukutWindow&) = delete;

  GtkWindow* Window() const { return GTK_WINDOW(window_); }
  void Present() { gtk_window_present(GTK_WINDOW(window_)); }
  void Start();  // first read and catalog, then polling (online only)

  void Apply(const HostState& state);
  void Apply(const Catalog& catalog);
  void SetSelectedMode(int mode) { selected_mode_ = mode; Render(); }
  void SetError(const std::string& error) { error_ = error; Render(); }

  // Navigation, also used by the --capture test.
  void ShowPage(Page page);
  void ShowSidebar();
  void OpenHelp();
  void CloseHelp();
  void OpenEasyEffectsHelp();  // the steps for EasyEffects (Using EasyEffects, a notice's How to fix)
  void OpenSnapAccessHelp();   // the one command that connects a snap's permissions
  void SetSnapAccessForTest(const std::string& command, bool audio_missing) { snap_command_ = command; snap_audio_missing_ = audio_missing; Render(); }
  void ShowSetup(int step);
  void HideSetup();
  void ScrollSettings(bool to_end);
  GtkWidget* FocusStart() const { return setup_step_ < 0 ? audio_row_ : setup_step_ == 0 ? setup_language_ : setup_next_; }
  const HostState& State() const { return state_; }
  GtkWidget* HelpDialog() const { return help_; }

  // Phone page (also set directly by the --capture test, which has no adb).
  void ShowPhoneTask(PhoneTask task, int step = 0);
  void SetPhoneState(std::vector<PhoneDevice> devices, std::optional<PhoneInfo> info, const std::string& message = "",
                     Severity severity = Severity::None);

 private:
  void Build();
  GtkWidget* BuildSidebar();
  GtkWidget* SidebarToggle(const char* name);  // a sidebar-show button tied to split_'s show-sidebar
  GtkWidget* BuildAudio();
  GtkWidget* BuildPlayback();
  GtkWidget* BuildSettings();
  GtkWidget* BuildSetup();
  GtkWidget* BuildPhone();
  void RenderPhone();
  void RenderPhoneCard();
  void RenderPhoneMessage();
  void PhoneChoices(GtkWidget* page);
  // needs_phone=false runs the action even when no phone can be used yet (Allow access).
  void PhoneAction(std::function<std::string(PhoneTools&, const PhoneDevice&)> action, bool needs_phone = true);
  void RefreshPhone();
  [[nodiscard]] std::string PhoneText(const std::string& english) const;
  void AddPhoneActions(GtkWidget* page, const char* en, const char* ar, const char* icon,
                       std::function<std::string(PhoneTools&, const PhoneDevice&)> action);
  GtkWidget* BuildOutputList(GtkWidget** list_out);
  GtkWidget* BuildBuffering(GtkWidget** group_out);

  void Render();
  void RenderStatus();
  void RenderModes();
  void RenderOutputs(GtkWidget* list);
  void RenderApps();
  void RenderSettings();
  void RenderSetup();
  void RenderBuffering(GtkWidget* group);

  // Host commands.
  void Send(std::vector<std::string> fields, std::function<void(bool, const std::string&)> done = {});
  void Poll();
  void RefreshCatalog();
  void Select(int mode);
  void Save(const std::string& key, const std::string& value);
  void ChooseMode(int mode);
  void ChooseOutput(const std::string& name);
  void ChooseApp(std::uint32_t pid);
  void ChooseBuffering(int level);
  void CommitBackground(int db);
  void CommitAmplification(int db);
  void SetLanguage(bool arabic);
  void SetTheme(const std::string& theme);
  void Toast(const std::string& text);
  void OpenSoundSettings();
  void RefreshSnapAccess();
  GtkWidget* SnapCommandBox(GtkWidget** label_out);
  void CopyDiagnostics();
  std::string AppIcon(const std::string& name);

  [[nodiscard]] bool Ar() const { return state_.Arabic(); }
  [[nodiscard]] const char* L(const char* en, const char* ar) const { return T(Ar(), en, ar); }
  [[nodiscard]] std::string ErrorText(const std::string& raw) const;
  [[nodiscard]] std::string NoticeText(const std::string& notice) const;
  [[nodiscard]] std::string ModeName(int mode) const;
  [[nodiscard]] std::string OutputShown() const;

  AdwApplication* app_;
  HostLink* link_;
  GtkWidget* window_ = nullptr;

  HostState state_;
  Catalog catalog_;
  bool have_state_ = false, built_arabic_ = false, built_ = false;
  int selected_mode_ = 1;
  bool command_ = false, polling_ = false, applying_ = false;
  bool banner_notice_ = false;  // the banner explains a notice; its button opens the steps
  // Snap only: the command that connects the missing permissions ("" = none missing).
  std::string snap_command_;
  bool snap_audio_missing_ = false, banner_snap_ = false;
  std::string error_;
  Page page_ = Page::Audio;
  int setup_step_ = -1;
  std::map<std::string, std::string> icon_cache_;
  bool icons_scanned_ = false;
  std::map<std::string, std::string> app_icons_;

  // Widgets of the current build (rebuilt when the language changes).
  GtkWidget* root_stack_ = nullptr;
  GtkWidget* toasts_ = nullptr;
  GtkWidget* split_ = nullptr;
  GtkWidget* sidebar_toggle_ = nullptr;
  GtkWidget* help_button_ = nullptr;
  GtkWidget* content_title_ = nullptr;
  GtkWidget* pages_ = nullptr;
  GtkWidget* banner_ = nullptr;
  GtkWidget* audio_row_ = nullptr;
  GtkWidget* settings_row_ = nullptr;
  GtkWidget* nav_top_ = nullptr;
  GtkWidget* nav_bottom_ = nullptr;
  GtkWidget* audio_columns_ = nullptr;
  GtkWidget* status_glyphs_ = nullptr;
  GtkWidget* status_icon_ = nullptr;
  GtkWidget* status_title_ = nullptr;
  GtkWidget* status_subtitle_ = nullptr;
  GtkWidget* power_button_ = nullptr;
  GtkWidget* power_label_ = nullptr;
  std::array<GtkWidget*, 3> mode_checks_{};
  GtkWidget* modes_list_ = nullptr;
  GtkWidget* background_card_ = nullptr;
  GtkWidget* background_value_ = nullptr;
  GtkWidget* background_scale_ = nullptr;
  GtkWidget* outputs_list_ = nullptr;
  GtkWidget* apps_list_ = nullptr;
  GtkWidget* refresh_button_ = nullptr;
  GtkWidget* easyeffects_button_ = nullptr;
  GtkWidget* buffering_group_ = nullptr;
  GtkWidget* help_ = nullptr;
  GtkWidget* settings_page_ = nullptr;
  GtkWidget* language_row_ = nullptr;
  GtkWidget* theme_row_ = nullptr;
  GtkWidget* tray_row_ = nullptr;
  GtkWidget* autostart_row_ = nullptr;
  GtkWidget* amp_scale_ = nullptr;
  GtkWidget* amp_value_ = nullptr;
  // Setup.
  GtkWidget* setup_stack_ = nullptr;
  GtkWidget* setup_step_label_ = nullptr;
  GtkWidget* setup_progress_ = nullptr;
  GtkWidget* setup_back_ = nullptr;
  GtkWidget* setup_next_ = nullptr;
  GtkWidget* setup_audio_dot_ = nullptr;
  GtkWidget* setup_audio_text_ = nullptr;
  GtkWidget* setup_tray_dot_ = nullptr;
  GtkWidget* setup_tray_text_ = nullptr;
  GtkWidget* setup_outputs_ = nullptr;
  GtkWidget* setup_buffering_ = nullptr;
  GtkWidget* setup_tray_hint_ = nullptr;
  GtkWidget* setup_language_ = nullptr;
  GtkWidget* setup_snap_card_ = nullptr;
  GtkWidget* setup_snap_command_ = nullptr;

  // Phone.
  std::shared_ptr<PhoneTools> phone_tools_;
  PhoneTask phone_task_ = PhoneTask::Choices;
  int phone_step_ = 0;
  bool phone_busy_ = false, phone_refreshing_ = false;
  std::vector<PhoneDevice> phone_devices_;
  std::string phone_serial_, phone_info_serial_, phone_message_, phone_card_note_;
  std::optional<PhoneInfo> phone_info_;
  Severity phone_severity_ = Severity::None;
  GtkWidget* phone_row_ = nullptr;
  GtkWidget* phone_content_ = nullptr;
  GtkWidget* phone_scroll_ = nullptr;
  GtkWidget* phone_footer_ = nullptr;
  GtkWidget* phone_back_ = nullptr;
  GtkWidget* phone_next_ = nullptr;
  GtkWidget* phone_step_label_ = nullptr;
  GtkWidget* phone_icon_ = nullptr;
  GtkWidget* phone_name_ = nullptr;
  GtkWidget* phone_connection_ = nullptr;
  GtkWidget* phone_version_ = nullptr;
  GtkWidget* phone_refresh_ = nullptr;
  GtkWidget* phone_allow_ = nullptr;
  GtkWidget* phone_picker_ = nullptr;
  GtkWidget* phone_actions_ = nullptr;
  GtkWidget* phone_progress_ = nullptr;
  GtkWidget* phone_message_box_ = nullptr;
  GtkWidget* phone_message_icon_ = nullptr;
  GtkWidget* phone_message_label_ = nullptr;
  guint phone_pulse_ = 0;
};

}  // namespace sukut::window
