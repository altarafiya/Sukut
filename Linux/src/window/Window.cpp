#include "window/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

#include <malloc.h>

#include "audio/Sandbox.h"
#include "window/Style.h"
#include "window/Widgets.h"

#ifndef SUKUT_VERSION
#define SUKUT_VERSION "0.4.0"
#endif

namespace sukut::window {
namespace {

constexpr const char* kAppId = "io.github.altarafiya.Sukut";
constexpr const char* kModeIcons[4] = {"system-shutdown-symbolic", "io.github.altarafiya.Sukut-music-filter-symbolic",
                                       "io.github.altarafiya.Sukut-sliders-symbolic", "changes-prevent-symbolic"};
constexpr const char* kBufferIcons[3] = {"io.github.altarafiya.Sukut-lightning-symbolic", "io.github.altarafiya.Sukut-sliders-symbolic",
                                         "io.github.altarafiya.Sukut-clock-symbolic"};

std::string Gain(int db) { return (db > 0 ? "+" : "") + std::to_string(db) + " dB"; }

std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// A horizontal scale whose value is committed on release (pointer) or after a
// short pause (keyboard, wheel), like Windows' ReleasedSlider.
struct CommitState {
  std::function<void(int)> changed, committed;
  const bool* applying = nullptr;
  bool pressed = false;
  guint pause = 0;
  GtkWidget* scale = nullptr;
};

GtkWidget* CommitScale(double min, double max, const bool* applying, std::function<void(int)> changed, std::function<void(int)> committed) {
  GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, 1);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
  gtk_range_set_round_digits(GTK_RANGE(scale), 0);
  gtk_widget_set_hexpand(scale, TRUE);
  auto* state = new CommitState{std::move(changed), std::move(committed), applying, false, 0, scale};
  g_object_set_data_full(G_OBJECT(scale), "commit", state, [](gpointer data) {
    auto* s = static_cast<CommitState*>(data);
    if (s->pause) g_source_remove(s->pause);
    delete s;
  });
  g_signal_connect(scale, "value-changed", G_CALLBACK(+[](GtkRange* range, gpointer data) {
    auto* s = static_cast<CommitState*>(data);
    if (*s->applying) return;
    const int value = static_cast<int>(std::lround(gtk_range_get_value(range)));
    if (s->changed) s->changed(value);
    if (s->pressed) return;
    if (s->pause) g_source_remove(s->pause);
    s->pause = g_timeout_add(500, [](gpointer d) -> gboolean {
      auto* st = static_cast<CommitState*>(d);
      st->pause = 0;
      st->committed(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(st->scale)))));
      return G_SOURCE_REMOVE;
    }, s);
  }), state);
  GtkEventController* legacy = gtk_event_controller_legacy_new();
  gtk_event_controller_set_propagation_phase(legacy, GTK_PHASE_CAPTURE);
  g_signal_connect(legacy, "event", G_CALLBACK(+[](GtkEventControllerLegacy*, GdkEvent* event, gpointer data) -> gboolean {
    auto* s = static_cast<CommitState*>(data);
    const auto type = gdk_event_get_event_type(event);
    if (type == GDK_BUTTON_PRESS || type == GDK_TOUCH_BEGIN) s->pressed = true;
    if ((type == GDK_BUTTON_RELEASE || type == GDK_TOUCH_END || type == GDK_TOUCH_CANCEL) && s->pressed) {
      s->pressed = false;
      if (s->pause) { g_source_remove(s->pause); s->pause = 0; }
      s->committed(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(s->scale)))));
    }
    return FALSE;
  }), state);
  gtk_widget_add_controller(scale, legacy);
  return scale;
}

GtkWidget* Section(const std::string& title, const char* icon_name, GtkWidget* suffix = nullptr) {
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  if (icon_name) gtk_box_append(GTK_BOX(header), Icon(icon_name));
  GtkWidget* label = Label(title, "section-heading", false);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(header), label);
  if (suffix) gtk_box_append(GTK_BOX(header), suffix);
  return header;
}

GtkWidget* BoxedList() {
  GtkWidget* list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list");
  return list;
}

void Clear(GtkWidget* list) {
  while (GtkWidget* child = gtk_widget_get_first_child(list)) gtk_list_box_remove(GTK_LIST_BOX(list), child);
}

// A row with a radio check at its start; activating the row checks it.
GtkWidget* RadioRow(const std::string& title, const std::string& subtitle, GtkWidget* icon, GtkWidget** check_out, GtkWidget* group) {
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
  if (!subtitle.empty()) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
  GtkWidget* check = gtk_check_button_new();
  gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
  if (group) gtk_check_button_set_group(GTK_CHECK_BUTTON(check), GTK_CHECK_BUTTON(group));
  // Prefixes stack towards the start, so the icon goes in first: radio, icon, title.
  if (icon) adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
  adw_action_row_add_prefix(ADW_ACTION_ROW(row), check);
  adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), check);
  *check_out = check;
  return row;
}

GtkWidget* FindScrolled(GtkWidget* widget) {
  if (GTK_IS_SCROLLED_WINDOW(widget)) return widget;
  for (GtkWidget* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
    if (GtkWidget* found = FindScrolled(child)) return found;
  return nullptr;
}

template <typename F>
void Connect(gpointer instance, const char* signal, F* callback, gpointer data) {
  g_signal_connect(instance, signal, G_CALLBACK(callback), data);
}

}  // namespace

SukutWindow::SukutWindow(AdwApplication* app, HostLink* link) : app_(app), link_(link) {
  // The --capture test (no host) never runs adb; it sets the phone state itself.
  if (link_) phone_tools_ = std::make_shared<PhoneTools>();
  window_ = adw_application_window_new(GTK_APPLICATION(app));
  gtk_window_set_title(GTK_WINDOW(window_), "سُكوت · Sukut");
  gtk_window_set_icon_name(GTK_WINDOW(window_), kAppId);
  gtk_window_set_default_size(GTK_WINDOW(window_), 1040, 720);
  gtk_widget_add_css_class(window_, "sukut");
  // The list of outputs and apps refreshes whenever the window becomes active.
  Connect(window_, "notify::is-active", +[](GObject* w, GParamSpec*, gpointer self) {
    if (gtk_window_is_active(GTK_WINDOW(w))) static_cast<SukutWindow*>(self)->RefreshCatalog();
  }, this);
  // Minimised: give memory back (audio runs in the host, so this never touches playback).
  Connect(window_, "realize", +[](GtkWidget* w, gpointer) {
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(w));
    g_signal_connect(surface, "notify::state", G_CALLBACK(+[](GObject* s, GParamSpec*, gpointer) {
      if (gdk_toplevel_get_state(GDK_TOPLEVEL(s)) & GDK_TOPLEVEL_STATE_MINIMIZED) malloc_trim(0);
    }), nullptr);
  }, nullptr);
  Build();
}

void SukutWindow::Start() {
  if (!link_) return;
  Poll();
  RefreshCatalog();
  g_timeout_add(1000, [](gpointer self) -> gboolean { static_cast<SukutWindow*>(self)->Poll(); return G_SOURCE_CONTINUE; }, this);
}

// ----- building ------------------------------------------------------------

void SukutWindow::Build() {
  built_ = true;
  built_arabic_ = Ar();
  gtk_widget_set_default_direction(Ar() ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
  SetClass(window_, "arabic", Ar());
  help_ = nullptr;
  setup_stack_ = nullptr;

  // GNOME's overlay split view: a sidebar button in the sidebar's header hides it;
  // while hidden, the same button in the page header brings it back. Below 600 sp
  // the sidebar floats over the content instead.
  split_ = adw_overlay_split_view_new();
  adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(split_), 230);  // room for the title beside its button
  adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(split_), 260);
  adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(split_), BuildSidebar());

  GtkWidget* content = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  content_title_ = adw_window_title_new("", "");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), content_title_);
  sidebar_toggle_ = SidebarToggle(L("Show sidebar", "إظهار الشريط الجانبي"));
  g_object_bind_property(split_, "show-sidebar", sidebar_toggle_, "visible", static_cast<GBindingFlags>(G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE));
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), sidebar_toggle_);
  // Audio help (modes and buffering), top right as on Windows 0.4; Audio page only.
  help_button_ = gtk_button_new_with_label("?");
  gtk_widget_add_css_class(help_button_, "flat");
  Name(help_button_, L("Audio modes and buffering help", "شرح أوضاع الصوت والتخزين المؤقت"));
  gtk_widget_set_tooltip_text(help_button_, L("Audio help", "دليل الصوت"));
  Connect(help_button_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->OpenHelp(); }, this);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), help_button_);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content), header);
  banner_ = adw_banner_new("");
  Connect(banner_, "button-clicked", +[](AdwBanner*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->banner_notice_) { s->OpenEasyEffectsHelp(); return; }
    s->error_.clear();
    s->Select(s->selected_mode_);
  }, this);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content), banner_);
  pages_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(pages_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_add_named(GTK_STACK(pages_), BuildAudio(), "audio");
  gtk_stack_add_named(GTK_STACK(pages_), BuildPhone(), "phone");
  gtk_stack_add_named(GTK_STACK(pages_), BuildSettings(), "settings");
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content), pages_);
  adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(split_), content);

  toasts_ = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toasts_), split_);
  root_stack_ = gtk_stack_new();
  gtk_stack_add_named(GTK_STACK(root_stack_), toasts_, "main");

  GtkWidget* bin = adw_breakpoint_bin_new();
  gtk_widget_set_size_request(bin, 420, 480);
  adw_breakpoint_bin_set_child(ADW_BREAKPOINT_BIN(bin), root_stack_);
  // Below 900 sp the Playback card moves under the modes; below 600 sp the sidebar
  // overlays the content and starts hidden.
  AdwBreakpoint* middle = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 899sp"));
  adw_breakpoint_add_setters(middle, G_OBJECT(audio_columns_), "orientation", GTK_ORIENTATION_VERTICAL,
                             G_OBJECT(audio_columns_), "homogeneous", FALSE, nullptr);
  adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(bin), middle);
  AdwBreakpoint* narrow = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 599sp"));
  adw_breakpoint_add_setters(narrow, G_OBJECT(audio_columns_), "orientation", GTK_ORIENTATION_VERTICAL,
                             G_OBJECT(audio_columns_), "homogeneous", FALSE,
                             G_OBJECT(split_), "collapsed", TRUE, G_OBJECT(split_), "show-sidebar", FALSE, nullptr);
  adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(bin), narrow);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), bin);

  const Page page = page_;
  page_ = Page::Audio;
  ShowPage(page);
  if (setup_step_ >= 0) ShowSetup(setup_step_);
  Render();
}

GtkWidget* SukutWindow::SidebarToggle(const char* name) {
  GtkWidget* toggle = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(toggle), "sidebar-show-symbolic");
  gtk_widget_set_tooltip_text(toggle, name);
  Name(toggle, name);
  g_object_bind_property(split_, "show-sidebar", toggle, "active", static_cast<GBindingFlags>(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));
  return toggle;
}

GtkWidget* SukutWindow::BuildSidebar() {
  GtkWidget* view = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("سُكوت · Sukut", ""));
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), SidebarToggle(L("Hide sidebar", "إخفاء الشريط الجانبي")));
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  auto row = [](const char* icon, const std::string& text) {
    GtkWidget* r = gtk_list_box_row_new();
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(content), Icon(icon));
    gtk_box_append(GTK_BOX(content), Label(text, nullptr, false));
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), content);
    gtk_accessible_update_property(GTK_ACCESSIBLE(r), GTK_ACCESSIBLE_PROPERTY_LABEL, text.c_str(), -1);
    g_object_set_data_full(G_OBJECT(r), "sukut-name", g_strdup(text.c_str()), g_free);
    return r;
  };
  nav_top_ = gtk_list_box_new();
  gtk_widget_add_css_class(nav_top_, "navigation-sidebar");
  audio_row_ = row("audio-speakers-symbolic", L("Audio", "الصوت"));
  gtk_list_box_append(GTK_LIST_BOX(nav_top_), audio_row_);
  phone_row_ = row("phone-symbolic", L("Phone", "الهاتف"));
  gtk_list_box_append(GTK_LIST_BOX(nav_top_), phone_row_);
  nav_bottom_ = gtk_list_box_new();
  gtk_widget_add_css_class(nav_bottom_, "navigation-sidebar");
  settings_row_ = row("preferences-system-symbolic", L("Settings", "الإعدادات"));
  gtk_list_box_append(GTK_LIST_BOX(nav_bottom_), settings_row_);
  GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(spacer, TRUE);
  gtk_box_append(GTK_BOX(box), nav_top_);
  gtk_box_append(GTK_BOX(box), spacer);
  gtk_box_append(GTK_BOX(box), nav_bottom_);
  for (GtkWidget* list : {nav_top_, nav_bottom_})
    Connect(list, "row-activated", +[](GtkListBox*, GtkListBoxRow* r, gpointer self) {
      auto* s = static_cast<SukutWindow*>(self);
      s->ShowPage(GTK_WIDGET(r) == s->settings_row_ ? Page::Settings : GTK_WIDGET(r) == s->phone_row_ ? Page::Phone : Page::Audio);
      // A floating sidebar closes once a page is chosen; a docked one stays.
      if (adw_overlay_split_view_get_collapsed(ADW_OVERLAY_SPLIT_VIEW(s->split_)))
        adw_overlay_split_view_set_show_sidebar(ADW_OVERLAY_SPLIT_VIEW(s->split_), FALSE);
    }, this);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), box);
  return view;
}

GtkWidget* SukutWindow::BuildAudio() {
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  GtkWidget* clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1040);
  adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 1040);
  audio_columns_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) edge(audio_columns_, 18);
  for (auto edge : {gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(audio_columns_, 18);

  GtkWidget* main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_hexpand(main, TRUE);

  // Status card: state, mode or Standby text, and Start/Stop.
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  AddClasses(card, {"card", "status-card"});
  status_glyphs_ = gtk_stack_new();
  gtk_widget_set_size_request(status_glyphs_, 36, 36);
  gtk_widget_set_valign(status_glyphs_, GTK_ALIGN_CENTER);
  status_icon_ = Icon("system-shutdown-symbolic", "status-glyph");
  gtk_stack_add_named(GTK_STACK(status_glyphs_), status_icon_, "icon");
  gtk_stack_add_named(GTK_STACK(status_glyphs_), SleepFace(36), "standby");
  gtk_box_append(GTK_BOX(card), status_glyphs_);
  GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
  status_title_ = Label("", "status-title");
  status_subtitle_ = Label("", "dim-label");
  gtk_widget_add_css_class(status_subtitle_, "caption");
  // A fixed size whatever the state: switching to Standby or Reconnecting never
  // moves the modes or the Playback card. Long text ends in "…", never widens.
  for (GtkWidget* label : {status_title_, status_subtitle_}) {
    gtk_label_set_wrap(GTK_LABEL(label), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 10);
  }
  // The subtitle may wrap to two lines; a two-line placeholder in the same stack
  // keeps that height reserved in every state.
  gtk_label_set_wrap(GTK_LABEL(status_subtitle_), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(status_subtitle_), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_lines(GTK_LABEL(status_subtitle_), 2);
  GtkWidget* reserve = Label("\n", "dim-label");
  gtk_widget_add_css_class(reserve, "caption");
  gtk_widget_set_opacity(reserve, 0);
  gtk_accessible_update_state(GTK_ACCESSIBLE(reserve), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
  GtkWidget* subtitle = gtk_stack_new();
  gtk_stack_add_named(GTK_STACK(subtitle), reserve, "reserve");
  gtk_stack_add_named(GTK_STACK(subtitle), status_subtitle_, "text");
  gtk_stack_set_visible_child(GTK_STACK(subtitle), status_subtitle_);
  gtk_box_append(GTK_BOX(text), status_title_);
  gtk_box_append(GTK_BOX(text), subtitle);
  gtk_box_append(GTK_BOX(card), text);
  power_button_ = gtk_button_new();
  GtkWidget* power_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(power_content, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(power_content), Icon("system-shutdown-symbolic"));
  power_label_ = gtk_label_new("");
  gtk_box_append(GTK_BOX(power_content), power_label_);
  gtk_button_set_child(GTK_BUTTON(power_button_), power_content);
  gtk_widget_add_css_class(power_button_, "power-button");
  gtk_widget_set_valign(power_button_, GTK_ALIGN_CENTER);
  Connect(power_button_, "clicked", +[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    s->Select(s->state_.mode == 0 ? s->selected_mode_ : 0);
  }, this);
  gtk_box_append(GTK_BOX(card), power_button_);
  gtk_box_append(GTK_BOX(main), card);

  // Audio mode in its own panel, like Playback (Windows 0.4); the help button is
  // in the header bar.
  GtkWidget* modes_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  AddClasses(modes_card, {"card", "playback-card"});
  gtk_box_append(GTK_BOX(modes_card), Section(L("Audio mode", "وضع الصوت"), kModeIcons[1]));
  modes_list_ = BoxedList();
  gtk_accessible_update_property(GTK_ACCESSIBLE(modes_list_), GTK_ACCESSIBLE_PROPERTY_LABEL, L("Audio mode", "وضع الصوت"), -1);
  const char* subtitles[3][2] = {{"Keep speech and other sounds", "الإبقاء على الكلام والأصوات الأخرى"},
                                 {"Adjust the background level", "ضبط مستوى أصوات الخلفية"},
                                 {"Focus on speech", "التركيز على الكلام"}};
  for (int m = 1; m <= 3; ++m) {
    const std::string tone = "mode-" + std::to_string(m);
    GtkWidget* icon = Icon(kModeIcons[m], "mode-icon");
    gtk_widget_add_css_class(icon, tone.c_str());
    if (m == 1) gtk_widget_set_direction(icon, GTK_TEXT_DIR_LTR);  // the Music Filter icon never mirrors
    GtkWidget* row = RadioRow(ModeName(m), L(subtitles[m - 1][0], subtitles[m - 1][1]), icon, &mode_checks_[m - 1],
                              m == 1 ? nullptr : mode_checks_[0]);
    g_object_set_data(G_OBJECT(mode_checks_[m - 1]), "mode", GINT_TO_POINTER(m));
    Connect(mode_checks_[m - 1], "toggled", +[](GtkCheckButton* check, gpointer self) {
      auto* s = static_cast<SukutWindow*>(self);
      if (s->applying_ || !gtk_check_button_get_active(check)) return;
      s->ChooseMode(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(check), "mode")));
    }, this);
    gtk_list_box_append(GTK_LIST_BOX(modes_list_), row);
  }
  gtk_box_append(GTK_BOX(modes_card), modes_list_);

  // Optimized's background level, inside the mode panel.
  background_card_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top(background_card_, 8);
  GtkWidget* bg_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* bg_title = Label(L("Background level", "مستوى الخلفية"), "section-heading", false);
  gtk_widget_set_hexpand(bg_title, TRUE);
  background_value_ = Label("", "accent-text", false);
  gtk_widget_add_css_class(background_value_, "heading");
  gtk_widget_set_direction(background_value_, GTK_TEXT_DIR_LTR);
  gtk_box_append(GTK_BOX(bg_header), bg_title);
  gtk_box_append(GTK_BOX(bg_header), background_value_);
  background_scale_ = CommitScale(-20, 10, &applying_,
      [this](int db) { gtk_label_set_text(GTK_LABEL(background_value_), Gain(db).c_str()); },
      [this](int db) { CommitBackground(db); });
  Name(background_scale_, L("Background level", "مستوى الخلفية"));
  gtk_widget_set_has_tooltip(background_scale_, FALSE);
  gtk_box_append(GTK_BOX(background_card_), bg_header);
  gtk_box_append(GTK_BOX(background_card_), background_scale_);
  gtk_box_append(GTK_BOX(modes_card), background_card_);
  gtk_box_append(GTK_BOX(main), modes_card);

  gtk_box_append(GTK_BOX(audio_columns_), main);
  // Side by side, the Start and mode column and Playback get equal widths;
  // stacked, each takes its own height (Build's breakpoints turn this off).
  gtk_box_set_homogeneous(GTK_BOX(audio_columns_), TRUE);
  gtk_box_append(GTK_BOX(audio_columns_), BuildPlayback());
  adw_clamp_set_child(ADW_CLAMP(clamp), audio_columns_);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
  return scroll;
}

GtkWidget* SukutWindow::BuildOutputList(GtkWidget** list_out) {
  *list_out = BoxedList();
  gtk_accessible_update_property(GTK_ACCESSIBLE(*list_out), GTK_ACCESSIBLE_PROPERTY_LABEL, L("Audio output", "مخرج الصوت"), -1);
  return *list_out;
}

GtkWidget* SukutWindow::BuildBuffering(GtkWidget** group_out) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* group = adw_toggle_group_new();
  adw_toggle_group_set_homogeneous(ADW_TOGGLE_GROUP(group), TRUE);
  gtk_accessible_update_property(GTK_ACCESSIBLE(group), GTK_ACCESSIBLE_PROPERTY_LABEL, L("Audio buffering", "التخزين المؤقت للصوت"), -1);
  const char* names[3][2] = {{"Lowest", "الأقل"}, {"Balanced", "متوازن"}, {"Maximum", "الأكبر"}};
  for (int level = 0; level < 3; ++level) {
    AdwToggle* toggle = adw_toggle_new();
    adw_toggle_set_name(toggle, std::to_string(level).c_str());
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    const std::string tone = "mode-" + std::to_string(level + 1);
    GtkWidget* icon = Icon(kBufferIcons[level], tone.c_str());
    gtk_box_append(GTK_BOX(content), icon);
    GtkWidget* label = gtk_label_new(L(names[level][0], names[level][1]));
    gtk_widget_add_css_class(label, tone.c_str());
    gtk_box_append(GTK_BOX(content), label);
    gtk_widget_add_css_class(content, "buffer-choice");
    adw_toggle_set_child(toggle, content);
    adw_toggle_set_tooltip(toggle, L(names[level][0], names[level][1]));
    adw_toggle_group_add(ADW_TOGGLE_GROUP(group), toggle);
  }
  Connect(group, "notify::active-name", +[](GObject* g, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    const char* name = adw_toggle_group_get_active_name(ADW_TOGGLE_GROUP(g));
    if (!s->applying_ && name) s->ChooseBuffering(std::atoi(name));
  }, this);
  gtk_box_append(GTK_BOX(box), group);
  GtkWidget* tip = Label(L("Higher buffering reduces interruptions and adds audio delay.", "زيادة التخزين المؤقت تقلّل التقطّع وتزيد تأخير الصوت."),
                         "dim-label");
  gtk_widget_add_css_class(tip, "caption");
  gtk_box_append(GTK_BOX(box), tip);
  *group_out = group;
  return box;
}

GtkWidget* SukutWindow::BuildPlayback() {
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  AddClasses(card, {"card", "playback-card"});
  gtk_widget_set_size_request(card, 320, -1);
  gtk_widget_set_valign(card, GTK_ALIGN_START);

  // One refresh beside Audio output re-reads outputs and source apps (Windows 0.4).
  GtkWidget* output = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  refresh_button_ = IconButton("view-refresh-symbolic", L("Refresh audio outputs and source apps", "تحديث مخارج الصوت والتطبيقات المصدر"));
  Connect(refresh_button_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->RefreshCatalog(); }, this);
  gtk_box_append(GTK_BOX(output), Section(L("Audio output", "مخرج الصوت"), "audio-headphones-symbolic", refresh_button_));
  gtk_box_append(GTK_BOX(output), BuildOutputList(&outputs_list_));
  // Shown while EasyEffects runs: the steps to have both (apps -> Sukut -> EasyEffects -> speakers).
  easyeffects_button_ = gtk_button_new_with_label(L("Using EasyEffects", "استخدام EasyEffects"));
  AddClasses(easyeffects_button_, {"flat", "caption"});
  gtk_widget_set_halign(easyeffects_button_, GTK_ALIGN_START);
  gtk_widget_set_visible(easyeffects_button_, FALSE);
  Connect(easyeffects_button_, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->OpenEasyEffectsHelp(); }, this);
  gtk_box_append(GTK_BOX(output), easyeffects_button_);
  gtk_box_append(GTK_BOX(card), output);
  gtk_box_append(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

  GtkWidget* source = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(source), Section(L("Source app", "التطبيق المصدر"), "computer-symbolic"));
  apps_list_ = BoxedList();
  gtk_accessible_update_property(GTK_ACCESSIBLE(apps_list_), GTK_ACCESSIBLE_PROPERTY_LABEL, L("Source app", "التطبيق المصدر"), -1);
  gtk_box_append(GTK_BOX(source), apps_list_);
  GtkWidget* hint = Label(L("Open an app, then refresh to add it here.", "افتح تطبيقًا ثم حدّث القائمة لإضافته هنا."), "dim-label");
  gtk_widget_add_css_class(hint, "caption");
  gtk_box_append(GTK_BOX(source), hint);
  gtk_box_append(GTK_BOX(card), source);

  gtk_box_append(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  GtkWidget* buffering = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);  // Windows 0.4: 16 between heading and choices
  gtk_box_append(GTK_BOX(buffering), Section(L("Audio buffering", "التخزين المؤقت للصوت"), "io.github.altarafiya.Sukut-lightning-symbolic"));
  gtk_box_append(GTK_BOX(buffering), BuildBuffering(&buffering_group_));
  gtk_box_append(GTK_BOX(card), buffering);
  return card;
}

GtkWidget* SukutWindow::BuildSettings() {
  settings_page_ = adw_preferences_page_new();
  auto group = [this](const std::string& title) {
    GtkWidget* g = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title.c_str());
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(settings_page_), ADW_PREFERENCES_GROUP(g));
    return g;
  };
  auto icon = [](GtkWidget* row, const char* name) { adw_action_row_add_prefix(ADW_ACTION_ROW(row), Icon(name)); };

  // General.
  GtkWidget* general = group(L("General", "عام"));
  language_row_ = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(language_row_), L("Language", "اللغة"));
  const char* languages[] = {"English", "العربية", nullptr};
  adw_combo_row_set_model(ADW_COMBO_ROW(language_row_), G_LIST_MODEL(gtk_string_list_new(languages)));
  icon(language_row_, "preferences-desktop-locale-symbolic");
  Connect(language_row_, "notify::selected", +[](GObject* row, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (!s->applying_) s->SetLanguage(adw_combo_row_get_selected(ADW_COMBO_ROW(row)) == 1);
  }, this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(general), language_row_);
  theme_row_ = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme_row_), L("App theme", "المظهر"));
  const char* themes[] = {L("System", "النظام"), L("Light", "فاتح"), L("Dark", "داكن"), nullptr};
  adw_combo_row_set_model(ADW_COMBO_ROW(theme_row_), G_LIST_MODEL(gtk_string_list_new(themes)));
  icon(theme_row_, "preferences-desktop-appearance-symbolic");
  Connect(theme_row_, "notify::selected", +[](GObject* row, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    if (!s->applying_) s->SetTheme(selected == 1 ? "light" : selected == 2 ? "dark" : "system");
  }, this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(general), theme_row_);
  tray_row_ = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tray_row_), L("Close to tray", "الإغلاق إلى منطقة الإشعارات"));
  icon(tray_row_, "go-down-symbolic");
  Connect(tray_row_, "notify::active", +[](GObject* row, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->applying_) return;
    s->state_.close_to_tray = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
    s->Save("CloseToTray", s->state_.close_to_tray ? "1" : "0");
  }, this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(general), tray_row_);
  autostart_row_ = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(autostart_row_), L("Start with system", "التشغيل مع النظام"));
  adw_action_row_set_subtitle(ADW_ACTION_ROW(autostart_row_),
                              L("Start Sukut in the tray when you sign in.", "يبدأ سُكوت في منطقة الإشعارات عند تسجيل الدخول."));
  icon(autostart_row_, "system-run-symbolic");
  Connect(autostart_row_, "notify::active", +[](GObject* row, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->applying_) return;
    const bool on = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
    s->Send({"settings", std::string("StartWithSystem=") + (on ? "1" : "0")}, [s, on](bool ok, const std::string& error) {
      if (ok) { s->state_.start_with_system = on; return; }
      s->Toast(error);
      s->Render();
    });
  }, this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(general), autostart_row_);
  gtk_widget_set_visible(autostart_row_, !InSandbox());  // writes to the autostart folder, outside the sandbox

  // Audio: amplification.
  GtkWidget* audio = group(L("Audio", "الصوت"));
  GtkWidget* amp_row = adw_preferences_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(amp_row), L("Audio amplification", "تضخيم الصوت"));
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(amp_row), FALSE);
  GtkWidget* amp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) edge(amp, 10);
  for (auto edge : {gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(amp, 12);
  GtkWidget* amp_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(amp_header), Icon("audio-volume-high-symbolic", "mode-2"));
  GtkWidget* amp_title = Label(L("Audio amplification", "تضخيم الصوت"), "section-heading", false);
  gtk_widget_set_hexpand(amp_title, TRUE);
  gtk_box_append(GTK_BOX(amp_header), amp_title);
  amp_value_ = Label("", "mode-2", false);
  gtk_widget_add_css_class(amp_value_, "heading");
  gtk_widget_set_direction(amp_value_, GTK_TEXT_DIR_LTR);
  gtk_box_append(GTK_BOX(amp_header), amp_value_);
  gtk_box_append(GTK_BOX(amp), amp_header);
  amp_scale_ = CommitScale(0, 12, &applying_, [this](int db) { gtk_label_set_text(GTK_LABEL(amp_value_), Gain(db).c_str()); },
                           [this](int db) { CommitAmplification(db); });
  for (int mark = 0; mark <= 12; mark += 3) gtk_scale_add_mark(GTK_SCALE(amp_scale_), mark, GTK_POS_BOTTOM, nullptr);
  Name(amp_scale_, L("Audio amplification", "تضخيم الصوت"));
  gtk_widget_set_has_tooltip(amp_scale_, FALSE);
  gtk_box_append(GTK_BOX(amp), amp_scale_);
  GtkWidget* note = Label(L("Processing modes only. Peak protection limits boost on loud audio.",
                            "يعمل مع أوضاع المعالجة فقط. تحدّ الحماية من القمم من التضخيم عندما يكون الصوت مرتفعًا."), "dim-label");
  gtk_widget_add_css_class(note, "caption");
  gtk_box_append(GTK_BOX(amp), note);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(amp_row), amp);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(audio), amp_row);

  // Shortcuts.
  GtkWidget* shortcuts = group(L("Shortcuts", "اختصارات"));
  GtkWidget* sound = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sound), L("Sound settings", "إعدادات الصوت"));
  adw_action_row_set_subtitle(ADW_ACTION_ROW(sound), L("The system's own volume and devices", "مستوى الصوت والأجهزة في النظام"));
  icon(sound, "multimedia-volume-control-symbolic");
  adw_action_row_add_suffix(ADW_ACTION_ROW(sound), Icon(Ar() ? "go-previous-symbolic" : "go-next-symbolic"));
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(sound), TRUE);
  Connect(sound, "activated", +[](AdwActionRow*, gpointer self) { static_cast<SukutWindow*>(self)->OpenSoundSettings(); }, this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(shortcuts), sound);
  gtk_widget_set_visible(sound, !InSandbox());  // the desktop's settings app can't be started from the sandbox
  GtkWidget* setup = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(setup), L("Setup guide", "دليل الإعداد"));
  icon(setup, "help-browser-symbolic");
  GtkWidget* open_guide = gtk_button_new_with_label(L("Open guide", "فتح الدليل"));
  gtk_widget_set_valign(open_guide, GTK_ALIGN_CENTER);
  Connect(open_guide, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->ShowSetup(0); }, this);
  adw_action_row_add_suffix(ADW_ACTION_ROW(setup), open_guide);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(shortcuts), setup);

  // Advanced.
  GtkWidget* advanced = group(L("Advanced", "متقدم"));
  GtkWidget* refresh = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(refresh), L("Refresh devices", "تحديث الأجهزة"));
  icon(refresh, "view-refresh-symbolic");
  GtkWidget* refresh_button = IconButton("view-refresh-symbolic", L("Refresh devices", "تحديث الأجهزة"));
  Connect(refresh_button, "clicked", +[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    s->Send({"refresh"}, [s](bool, const std::string&) { s->RefreshCatalog(); });
  }, this);
  adw_action_row_add_suffix(ADW_ACTION_ROW(refresh), refresh_button);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(advanced), refresh);
  GtkWidget* diagnostics = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(diagnostics), L("Diagnostics", "التشخيص"));
  adw_action_row_set_subtitle(ADW_ACTION_ROW(diagnostics), L("Copy details to include in a problem report", "نسخ التفاصيل لإرفاقها مع بلاغ عن مشكلة"));
  icon(diagnostics, "dialog-information-symbolic");
  GtkWidget* copy = IconButton("edit-copy-symbolic", L("Copy diagnostics", "نسخ التشخيص"));
  Connect(copy, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->CopyDiagnostics(); }, this);
  adw_action_row_add_suffix(ADW_ACTION_ROW(diagnostics), copy);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(advanced), diagnostics);

  // About, last, like the phone.
  GtkWidget* about_group = group(L("About", "حول"));
  GtkWidget* about = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  AddClasses(about, {"card", "about-card"});
  GtkWidget* logo = gtk_image_new_from_icon_name(kAppId);
  gtk_image_set_pixel_size(GTK_IMAGE(logo), 64);
  gtk_accessible_update_property(GTK_ACCESSIBLE(logo), GTK_ACCESSIBLE_PROPERTY_LABEL, "Sukut", -1);
  gtk_box_append(GTK_BOX(about), logo);
  auto centered = [](GtkWidget* label) { gtk_label_set_xalign(GTK_LABEL(label), 0.5); gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER); return label; };
  gtk_box_append(GTK_BOX(about), centered(Label("سُكوت · Sukut", "about-name", false)));
  GtkWidget* version = centered(Label(SUKUT_VERSION, "dim-label", false));
  gtk_widget_set_direction(version, GTK_TEXT_DIR_LTR);
  gtk_widget_add_css_class(version, "caption");
  gtk_box_append(GTK_BOX(about), version);
  GtkWidget* developed = centered(Label(L("Developed by", "تطوير"), "dim-label", false));
  gtk_widget_add_css_class(developed, "caption");
  gtk_box_append(GTK_BOX(about), developed);
  GtkWidget* credit = BrandCredit();
  gtk_box_append(GTK_BOX(about), credit);
  GtkWidget* fonts = centered(Label(L("IBM Plex Sans Arabic · Handjet · SIL OFL", "IBM Plex Sans Arabic · Handjet · رخصة SIL OFL"),
                                    "dim-label"));
  gtk_widget_add_css_class(fonts, "caption");
  gtk_box_append(GTK_BOX(about), fonts);
  // The version is isolated as one left-to-right run (U+2066…U+2069), so Arabic
  // text around it can't reorder "0.4.0-preview" into "preview-0.4.0".
  GtkWidget* changes = adw_expander_row_new();
  adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(changes), FALSE);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(changes),
                                (std::string(L("What’s new · ", "الجديد · ")) + "\xE2\x81\xA6" SUKUT_VERSION "\xE2\x81\xA9").c_str());
  GtkWidget* changes_list = BoxedList();
  gtk_widget_set_margin_top(changes_list, 8);
  gtk_list_box_append(GTK_LIST_BOX(changes_list), changes);
  GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(list, 12);
  const char* items[4][2] = {
      {"A GNOME window with a sidebar, Start and Stop, mode rows and an always-visible Playback card.",
       "نافذة GNOME بشريط جانبي وزر تشغيل وإيقاف وصفوف للأوضاع وبطاقة تشغيل ظاهرة دائمًا."},
      {"Audio through PipeWire, with no driver to install. Sukut follows the output you pick and gives your audio back on exit.",
       "الصوت عبر PipeWire دون تثبيت أي برنامج تشغيل. يتبع سُكوت المخرج الذي تختاره ويعيد صوتك كما كان عند الخروج."},
      {"A tray menu with the modes, buffering, output and source app. Closing the window keeps Sukut there.",
       "قائمة في منطقة الإشعارات بالأوضاع والتخزين المؤقت والمخرج والتطبيق المصدر، ويبقى سُكوت فيها عند إغلاق النافذة."},
      {"The same sealed audio engine as Windows and Android.", "محرك الصوت المحمي نفسه المستخدم في ويندوز وأندرويد."}};
  for (const auto& item : items) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* bullet = gtk_label_new("•");
    AddClasses(bullet, {"mode-1", "heading"});
    gtk_widget_set_valign(bullet, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), bullet);
    GtkWidget* text = Label(L(item[0], item[1]));
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(list), row);
  }
  GtkWidget* changes_row = gtk_list_box_row_new();
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(changes_row), FALSE);
  gtk_widget_set_focusable(changes_row, FALSE);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(changes_row), list);
  adw_expander_row_add_row(ADW_EXPANDER_ROW(changes), changes_row);
  gtk_box_append(GTK_BOX(about), changes_list);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about_group), about);
  return settings_page_;
}

// ----- navigation ----------------------------------------------------------

void SukutWindow::ShowPage(Page page) {
  page_ = page;
  const bool settings = page == Page::Settings, phone = page == Page::Phone;
  const char* title = settings ? L("Settings", "الإعدادات") : phone ? L("Phone", "الهاتف") : L("Audio", "الصوت");
  gtk_stack_set_visible_child_name(GTK_STACK(pages_), settings ? "settings" : phone ? "phone" : "audio");
  gtk_widget_set_visible(help_button_, !settings && !phone);
  adw_window_title_set_title(ADW_WINDOW_TITLE(content_title_), title);
  adw_window_title_set_subtitle(ADW_WINDOW_TITLE(content_title_),
                                settings ? "" : phone ? L("Install and activate Sukut on Android", "تثبيت سُكوت وتفعيله على أندرويد")
                                                      : L("Output, source and filtering", "مخرج الصوت والمصدر والترشيح"));
  gtk_list_box_select_row(GTK_LIST_BOX(nav_top_), settings ? nullptr : GTK_LIST_BOX_ROW(phone ? phone_row_ : audio_row_));
  gtk_list_box_select_row(GTK_LIST_BOX(nav_bottom_), settings ? GTK_LIST_BOX_ROW(settings_row_) : nullptr);
  if (settings) RefreshCatalog();
  if (phone) RefreshPhone();
}

void SukutWindow::ShowSidebar() { adw_overlay_split_view_set_show_sidebar(ADW_OVERLAY_SPLIT_VIEW(split_), TRUE); }

void SukutWindow::ScrollSettings(bool to_end) {
  if (GtkWidget* scrolled = FindScrolled(settings_page_)) {
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    gtk_adjustment_set_value(adjustment, to_end ? gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment) : 0);
  }
}

void SukutWindow::OpenHelp() {
  if (help_) return;
  help_ = GTK_WIDGET(adw_dialog_new());
  adw_dialog_set_title(ADW_DIALOG(help_), L("Audio help", "دليل الصوت"));
  adw_dialog_set_content_width(ADW_DIALOG(help_), 900);
  adw_dialog_set_content_height(ADW_DIALOG(help_), 620);
  gtk_widget_set_size_request(help_, 360, 320);
  GtkWidget* view = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);
  adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), FALSE);
  GtkWidget* done = gtk_button_new_with_label(L("Done", "تم"));
  gtk_widget_add_css_class(done, "suggested-action");
  Connect(done, "clicked", +[](GtkButton*, gpointer self) { static_cast<SukutWindow*>(self)->CloseHelp(); }, this);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), done);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  GtkWidget* columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_box_set_homogeneous(GTK_BOX(columns), TRUE);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(columns, 18);
  auto section = [](const std::string& title) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(box), Label(title, "help-section"));
    return box;
  };
  GtkWidget* modes = section(L("Audio modes", "أوضاع الصوت"));
  gtk_box_append(GTK_BOX(modes), HelpCard(kModeIcons[1], "mode-1", ModeName(1),
      {L("Removes music while keeping speech and other sounds.", "يزيل الموسيقى مع إبقاء الكلام والأصوات الأخرى."),
       L("A good starting point for everyday listening.", "خيار مناسب للاستماع اليومي."),
       L("Music Filter is not perfect. Some instruments may still be heard.", "مرشّح الموسيقى ليس مثاليًا. قد تبقى بعض الآلات مسموعة.")}));
  gtk_box_append(GTK_BOX(modes), HelpCard(kModeIcons[2], "mode-2", ModeName(2),
      {L("Removes music and lets you choose how much background sound remains.", "يزيل الموسيقى ويتيح لك تحديد مقدار أصوات الخلفية."),
       L("Use the Background level slider: lower for a quieter background, higher for more ambience.",
         "استخدم شريط «مستوى الخلفية»: خفّضه لخلفية أهدأ، وارفعه لسماع أصوات أكثر.")}));
  gtk_box_append(GTK_BOX(modes), HelpCard(kModeIcons[3], "mode-3", ModeName(3),
      {L("Focuses on speech by muting music and background sound.", "يركّز على الكلام بكتم الموسيقى وأصوات الخلفية."),
       L("Some sound effects may also disappear.", "قد تختفي بعض المؤثرات الصوتية أيضًا.")}));
  gtk_box_append(GTK_BOX(modes), HelpCard("system-shutdown-symbolic", "accent-text", L("Start and stop", "التشغيل والإيقاف"),
      {L("Choose a mode, then click Start. Click Stop to turn filtering off. Selecting a mode while stopped does not start audio.",
         "اختر وضعًا، ثم اضغط «تشغيل». اضغط «إيقاف» لإيقاف الترشيح. اختيار الوضع أثناء التوقف لا يبدأ معالجة الصوت.")}));
  GtkWidget* buffering = section(L("Audio buffering", "التخزين المؤقت للصوت"));
  gtk_box_append(GTK_BOX(buffering), HelpCard("dialog-information-symbolic", "accent-text", L("What does buffering do?", "ما هو التخزين المؤقت؟"),
      {L("Sukut keeps a short queue of processed sound ready to play. This helps sound stay smooth when your PC briefly gets busy.",
         "يُبقي سُكوت كمية قصيرة من الصوت المعالَج جاهزة للتشغيل، ليبقى الصوت سلسًا عندما ينشغل الحاسوب قليلًا.")}));
  gtk_box_append(GTK_BOX(buffering), HelpCard(kBufferIcons[0], "mode-1", L("Lowest · less waiting", "الأقل · انتظار أقصر"),
      {L("Adds the least buffering delay. Choose it if playback stays smooth.", "يضيف أقل تأخير للتخزين المؤقت. اختره إذا استمر الصوت بسلاسة.")}));
  gtk_box_append(GTK_BOX(buffering), HelpCard(kBufferIcons[1], "mode-2", L("Balanced · start here", "متوازن · ابدأ به"),
      {L("A little more delay, with more protection against short pauses. A good everyday choice.",
         "تأخير أكبر قليلًا مقابل حماية أفضل من التقطّع القصير. خيار مناسب للاستخدام اليومي.")}));
  gtk_box_append(GTK_BOX(buffering), HelpCard(kBufferIcons[2], "mode-3", L("Maximum · fewer interruptions", "الأكبر · انقطاعات أقل"),
      {L("Keeps more sound ready. Try it if audio still cuts out; it adds the most buffering delay.",
         "يُبقي صوتًا أكثر جاهزًا. جرّبه إذا استمر التقطّع؛ لكنه يضيف أكبر تأخير للتخزين المؤقت.")}));
  gtk_box_append(GTK_BOX(buffering), HelpCard("camera-video-symbolic", "accent-text", L("Audio delay and video sync", "تأخير الصوت وتزامن الفيديو"),
      {L("Delay means hearing a sound after the action that caused it. For example, lips move before you hear the words. This mismatch is called desync.",
         "التأخير هو سماع الصوت بعد الحدث الذي سبّبه، مثل تحرّك الشفاه قبل سماع الكلام. يُسمّى هذا عدم التزامن."),
       L("More buffering can increase that mismatch. Use the lowest setting that plays smoothly. Music removal itself also takes time; Lowest does not mean zero delay.",
         "قد يزيد التخزين المؤقت هذا الفارق. استخدم أقل خيار يعمل بسلاسة. إزالة الموسيقى نفسها تستغرق وقتًا أيضًا؛ لذا فالأقل لا يعني انعدام التأخير.")}));
  gtk_box_append(GTK_BOX(columns), modes);
  gtk_box_append(GTK_BOX(columns), buffering);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), columns);
  gtk_widget_set_vexpand(scroll, TRUE);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);
  adw_dialog_set_child(ADW_DIALOG(help_), view);
  // Start on Done, the default button (as on Windows), so focus never rests on the sheet itself.
  adw_dialog_set_default_widget(ADW_DIALOG(help_), done);
  adw_dialog_set_focus(ADW_DIALOG(help_), done);
  // Narrow: one column.
  AdwBreakpoint* narrow = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 640sp"));
  adw_breakpoint_add_setters(narrow, G_OBJECT(columns), "orientation", GTK_ORIENTATION_VERTICAL, G_OBJECT(columns), "homogeneous", FALSE, nullptr);
  adw_dialog_add_breakpoint(ADW_DIALOG(help_), narrow);
  Connect(help_, "closed", +[](AdwDialog*, gpointer self) { static_cast<SukutWindow*>(self)->help_ = nullptr; }, this);
  adw_dialog_present(ADW_DIALOG(help_), window_);
}

void SukutWindow::CloseHelp() {
  if (help_) adw_dialog_close(ADW_DIALOG(help_));
}

// ----- rendering -----------------------------------------------------------

std::string SukutWindow::ModeName(int mode) const {
  switch (mode) {
    case 2: return L("Optimized", "محسّن");
    case 3: return L("Strict", "صارم");
    default: return L("Music Filter", "مرشّح الموسيقى");
  }
}

std::string SukutWindow::ErrorText(const std::string& raw) const {
  if (raw == "source_closed" || raw == "source app closed")
    return L("Source app closed. Select a mode for all system audio, or choose another Source app.",
             "أُغلق التطبيق المصدر. اختر وضعًا لصوت النظام بالكامل، أو اختر تطبيقًا مصدرًا آخر.");
  if (raw.rfind("Sukut's audio service", 0) == 0) return L(raw.c_str(), "توقفت خدمة صوت سُكوت عن الاستجابة.");
  return Ar() ? std::string("توقف الصوت عن الاستجابة. أعد تشغيل الصوت. (") + raw + ")" : raw;
}

// Problems filtering survives (the host's EngineState::notice).
std::string SukutWindow::NoticeText(const std::string& notice) const {
  const auto colon = notice.find(':');
  const std::string code = notice.substr(0, colon), detail = colon == std::string::npos ? "" : notice.substr(colon + 1);
  if (code == "feedback_loop")
    return detail + L(" sent Sukut's sound back into Sukut, so Sukut now plays to another output.",
                      " يعيد صوت سُكوت إلى سُكوت، لذا يشغّل سُكوت الصوت الآن على مخرج آخر.");
  if (code == "easyeffects_process_all")
    return std::string(L("EasyEffects processes all output streams, so ", "يعالج EasyEffects كل مسارات الإخراج، لذا يتجاوز ")) + detail +
           L(" skips Sukut.", " سُكوت.");
  if (code == "stream_pinned")
    return detail + L(" is set to play on a specific output, so it skips Sukut.", " مضبوط على مخرج صوت محدد، لذا يتجاوز سُكوت.");
  return {};
}

// The order that works: apps -> Sukut -> EasyEffects -> speakers. EasyEffects
// must not take apps itself, and must not follow the default output (Sukut's
// sink while Sukut runs), or the sound loops.
void SukutWindow::OpenEasyEffectsHelp() {
  AdwDialog* dialog = adw_alert_dialog_new(
      L("Sukut with EasyEffects", "سُكوت مع EasyEffects"),
      L("To hear EasyEffects while Sukut filters, the sound goes: apps → Sukut → EasyEffects → speakers.",
        "لتسمع EasyEffects أثناء ترشيح سُكوت، يمر الصوت هكذا: التطبيقات ← سُكوت ← EasyEffects ← السماعات."));
  // The steps, aligned to the reading direction (the alert's body is centred).
  GtkWidget* steps = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  for (const char* text : {
           L("1. In EasyEffects, open Preferences and turn off “Process all output streams”.",
             "1. في EasyEffects، افتح التفضيلات (Preferences) وأوقف خيار «Process all output streams»."),
           L("2. In EasyEffects’ PipeWire page, turn off “Use default output” and choose your speakers or headphones as its output device.",
             "2. في صفحة PipeWire في EasyEffects، أوقف «Use default output» واختر مكبر الصوت أو سماعات الرأس جهازًا لإخراجه."),
           L("3. In your system’s sound settings, choose “Easy Effects Sink” as the output.",
             "3. في إعدادات الصوت في نظامك، اختر «Easy Effects Sink» مخرجًا للصوت."),
           L("4. In Sukut, choose “Easy Effects Sink” under Audio output.", "4. في سُكوت، اختر «Easy Effects Sink» تحت مخرج الصوت."),
       }) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_append(GTK_BOX(steps), label);
  }
  for (const char* text : {
           L("Headphones you connect while Sukut is on are used at once; when they disconnect, Sukut goes back to Easy Effects Sink.",
             "سماعات الرأس التي توصلها وسُكوت يعمل تُستخدم فورًا، وعند فصلها يعود سُكوت إلى Easy Effects Sink."),
           L("If an app still skips Sukut, it was moved to a specific output in the sound settings. Move it back to the default output there.",
             "إن ظلّ تطبيق يتجاوز سُكوت، فقد نُقل إلى مخرج محدد في إعدادات الصوت. أعده هناك إلى المخرج الافتراضي."),
       }) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_set_margin_top(label, 6);
    gtk_box_append(GTK_BOX(steps), label);
  }
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), steps);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", L("Close", "إغلاق"));
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "close");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "close");
  adw_dialog_present(dialog, window_);
}

std::string SukutWindow::OutputShown() const {
  if (!state_.output.empty()) return state_.output;
  if (!state_.chosen_output.empty()) return state_.chosen_output;
  return catalog_.default_output;
}

void SukutWindow::Apply(const HostState& state) {
  const bool first = !have_state_;
  const bool language_changed = state.language != state_.language;
  state_ = state;
  have_state_ = true;
  if (state_.mode != 0) selected_mode_ = state_.mode;
  else if (!command_) selected_mode_ = state_.preferred_mode;
  AdwStyleManager* manager = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(manager, state_.theme == "light" ? ADW_COLOR_SCHEME_FORCE_LIGHT
                                              : state_.theme == "dark" ? ADW_COLOR_SCHEME_FORCE_DARK
                                                                       : ADW_COLOR_SCHEME_DEFAULT);
  if (language_changed || built_arabic_ != Ar()) Build();
  if (first && !state_.setup_completed && setup_step_ < 0) ShowSetup(0);
  Render();
}

void SukutWindow::Apply(const Catalog& catalog) {
  catalog_ = catalog;
  Render();
}

void SukutWindow::Render() {
  if (!built_) return;
  applying_ = true;
  RenderStatus();
  RenderModes();
  RenderOutputs(outputs_list_);
  RenderApps();
  RenderBuffering(buffering_group_);
  RenderSettings();
  RenderSetup();
  RenderPhoneCard();
  // Problems: the source app closed, the engine needs attention, or a notice
  // filtering survives (an audio loop, an app pinned past Sukut).
  std::string banner;
  bool restart = false;
  banner_notice_ = false;
  if (!error_.empty()) { banner = ErrorText(error_); restart = error_ != "source app closed"; }
  else if (state_.state == 2) { banner = ErrorText(state_.attention); restart = state_.attention != "source_closed"; }
  else if (state_.mode != 0 && !state_.notice.empty()) { banner = NoticeText(state_.notice); banner_notice_ = !banner.empty(); }
  adw_banner_set_title(ADW_BANNER(banner_), banner.c_str());
  adw_banner_set_button_label(ADW_BANNER(banner_), banner_notice_ ? L("How to fix", "طريقة الإصلاح")
                                                   : restart       ? L("Restart audio", "إعادة تشغيل الصوت")
                                                                   : nullptr);
  adw_banner_set_revealed(ADW_BANNER(banner_), !banner.empty());
  const bool usable = !command_ && !state_.busy;
  for (GtkWidget* w : {power_button_, modes_list_, background_scale_, outputs_list_, apps_list_, refresh_button_, buffering_group_, amp_scale_})
    gtk_widget_set_sensitive(w, usable);
  applying_ = false;
}

void SukutWindow::RenderStatus() {
  enum class Look { Off, On, Standby, Attention } look;
  const bool working = command_ || state_.busy || state_.reconnecting;
  if (state_.state == 2 || !error_.empty()) look = Look::Attention;
  else if (state_.mode != 0 && state_.standby && !working) look = Look::Standby;
  else if (state_.mode != 0 || command_) look = Look::On;
  else look = Look::Off;
  // The Windows status words (ShellView.StatusText).
  std::string status;
  if (command_) status = L("Starting…", "جارٍ التشغيل…");
  else if (state_.busy) status = L("Preparing", "جارٍ التجهيز");
  else if (state_.reconnecting) status = L("Reconnecting", "إعادة الاتصال");
  else if (look == Look::Attention) status = L("Attention", "تنبيه");
  else if (look == Look::Standby) status = L("Standby", "استعداد");
  else if (state_.mode != 0) status = L("On", "يعمل");
  else status = L("Off", "متوقف");
  gtk_label_set_text(GTK_LABEL(status_title_), status.c_str());
  // Standby: the first sentence of the Windows text fits the card; the whole
  // explanation is the tooltip and the accessible description.
  const char* rest = L("Nothing is playing. Sukut rests to save power and wakes as soon as sound starts.",
                       "لا يوجد صوت قيد التشغيل. يرتاح سُكوت لتوفير الطاقة ويعود فور بدء الصوت.");
  gtk_label_set_text(GTK_LABEL(status_subtitle_),
                     look == Look::Standby ? L("Nothing is playing", "لا يوجد صوت قيد التشغيل") : ModeName(selected_mode_).c_str());
  gtk_widget_set_tooltip_text(status_subtitle_, look == Look::Standby ? rest : nullptr);
  gtk_accessible_update_property(GTK_ACCESSIBLE(status_subtitle_), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, look == Look::Standby ? rest : "", -1);
  gtk_stack_set_visible_child_name(GTK_STACK(status_glyphs_), look == Look::Standby ? "standby" : "icon");
  const char* glyph = look == Look::Attention ? "dialog-warning-symbolic"
                      : working              ? "io.github.altarafiya.Sukut-clock-symbolic"
                      : look == Look::On     ? "media-playback-start-symbolic"
                                             : "system-shutdown-symbolic";
  gtk_image_set_from_icon_name(GTK_IMAGE(status_icon_), glyph);
  for (const char* c : {"off", "on", "attention", "working"}) gtk_widget_remove_css_class(status_icon_, c);
  gtk_widget_add_css_class(status_icon_, look == Look::Attention ? "attention" : working ? "working" : look == Look::On ? "on" : "off");
  for (const char* c : {"off", "on", "attention"}) gtk_widget_remove_css_class(power_button_, c);
  gtk_widget_add_css_class(power_button_, look == Look::Attention ? "attention" : look == Look::Off ? "off" : "on");
  gtk_label_set_text(GTK_LABEL(power_label_), command_ ? L("Working…", "جارٍ التنفيذ…") : state_.mode == 0 ? L("Start", "تشغيل") : L("Stop", "إيقاف"));
  Name(power_button_, state_.mode == 0 ? std::string(L("Turn on ", "تشغيل ")) + ModeName(selected_mode_) : L("Turn off Sukut", "إيقاف سُكوت"));
}

void SukutWindow::RenderModes() {
  for (int m = 1; m <= 3; ++m) gtk_check_button_set_active(GTK_CHECK_BUTTON(mode_checks_[m - 1]), selected_mode_ == m);
  gtk_widget_set_visible(background_card_, selected_mode_ == 2);
  if (!static_cast<CommitState*>(g_object_get_data(G_OBJECT(background_scale_), "commit"))->pressed)
    gtk_range_set_value(GTK_RANGE(background_scale_), state_.background_db);
  gtk_label_set_text(GTK_LABEL(background_value_), Gain(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(background_scale_))))).c_str());
}

void SukutWindow::RenderOutputs(GtkWidget* list) {
  if (!list) return;
  const std::string shown = OutputShown();
  std::string signature = shown + "|" + (Ar() ? "ar" : "en");
  for (const auto& o : catalog_.outputs) signature += "|" + o.name + "=" + o.description;
  const char* previous = static_cast<const char*>(g_object_get_data(G_OBJECT(list), "signature"));
  if (list == outputs_list_ && easyeffects_button_)
    gtk_widget_set_visible(easyeffects_button_, std::any_of(catalog_.outputs.begin(), catalog_.outputs.end(),
                                                            [](const auto& o) { return o.name == "easyeffects_sink"; }));
  if (previous && signature == previous) return;
  g_object_set_data_full(G_OBJECT(list), "signature", g_strdup(signature.c_str()), g_free);
  Clear(list);
  GtkWidget* group = nullptr;
  if (catalog_.outputs.empty()) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), catalog_.read ? L("No speakers or headphones found", "لم يُعثر على سماعات أو مكبر صوت")
                                                                          : L("Looking for devices…", "جارٍ البحث عن الأجهزة…"));
    gtk_widget_add_css_class(row, "dim-label");
    gtk_list_box_append(GTK_LIST_BOX(list), row);
    return;
  }
  for (const auto& output : catalog_.outputs) {
    const std::string lower = Lower(output.name + " " + output.description);
    // Headphones, a display or speakers, from the name (Windows 0.4's mapping); unknown names are speakers.
    const auto has = [&](const char* word) { return lower.find(word) != std::string::npos; };
    const char* icon = has("headphone") || has("headset") ? "audio-headphones-symbolic"
                       : has("hdmi") || has("displayport") || has("monitor") ? "video-display-symbolic"
                                                                                : "audio-speakers-symbolic";
    GtkWidget* check = nullptr;
    GtkWidget* row = RadioRow(output.description, "", Icon(icon), &check, group);
    if (!group) group = check;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), output.name == shown);
    g_object_set_data_full(G_OBJECT(check), "output", g_strdup(output.name.c_str()), g_free);
    Connect(check, "toggled", +[](GtkCheckButton* c, gpointer self) {
      auto* s = static_cast<SukutWindow*>(self);
      if (!s->applying_ && gtk_check_button_get_active(c)) s->ChooseOutput(static_cast<const char*>(g_object_get_data(G_OBJECT(c), "output")));
    }, this);
    gtk_list_box_append(GTK_LIST_BOX(list), row);
  }
}

std::string SukutWindow::AppIcon(const std::string& name) {
  if (!icons_scanned_) {
    icons_scanned_ = true;
    GList* all = g_app_info_get_all();
    for (GList* item = all; item; item = item->next) {
      GAppInfo* info = G_APP_INFO(item->data);
      GIcon* icon = g_app_info_get_icon(info);
      if (!icon) continue;
      gchar* text = g_icon_to_string(icon);
      if (!text) continue;
      for (const char* key : {g_app_info_get_name(info), g_app_info_get_executable(info)}) {
        if (!key || !*key) continue;
        gchar* base = g_path_get_basename(key);
        app_icons_.emplace(Lower(base), text);
        g_free(base);
      }
      g_free(text);
    }
    g_list_free_full(all, g_object_unref);
  }
  const auto found = app_icons_.find(Lower(name));
  return found == app_icons_.end() ? std::string() : found->second;
}

void SukutWindow::RenderApps() {
  std::string signature = std::to_string(state_.source_pid) + "|" + (Ar() ? "ar" : "en");
  for (const auto& a : catalog_.apps) signature += "|" + std::to_string(a.pid) + "=" + a.name;
  const char* previous = static_cast<const char*>(g_object_get_data(G_OBJECT(apps_list_), "signature"));
  if (previous && signature == previous) return;
  g_object_set_data_full(G_OBJECT(apps_list_), "signature", g_strdup(signature.c_str()), g_free);
  Clear(apps_list_);
  std::vector<Catalog::App> apps{{0, L("All system audio", "صوت النظام بالكامل")}};
  apps.insert(apps.end(), catalog_.apps.begin(), catalog_.apps.end());
  if (std::none_of(apps.begin(), apps.end(), [&](const auto& a) { return a.pid == state_.source_pid; }))
    apps.push_back({state_.source_pid, L("Selected app (unavailable)", "التطبيق المحدد (غير متاح)")});
  GtkWidget* group = nullptr;
  for (const auto& app : apps) {
    GtkWidget* icon;
    const std::string icon_text = app.pid ? AppIcon(app.name) : std::string();
    if (!icon_text.empty()) {
      GIcon* gicon = g_icon_new_for_string(icon_text.c_str(), nullptr);
      icon = gtk_image_new_from_gicon(gicon);
      if (gicon) g_object_unref(gicon);
      gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
    } else {
      icon = Icon(app.pid ? "application-x-executable-symbolic" : "computer-symbolic");
    }
    GtkWidget* check = nullptr;
    GtkWidget* row = RadioRow(app.name, "", icon, &check, group);
    if (!group) group = check;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), app.pid == state_.source_pid);
    g_object_set_data(G_OBJECT(check), "pid", GUINT_TO_POINTER(app.pid));
    Connect(check, "toggled", +[](GtkCheckButton* c, gpointer self) {
      auto* s = static_cast<SukutWindow*>(self);
      if (!s->applying_ && gtk_check_button_get_active(c)) s->ChooseApp(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(c), "pid")));
    }, this);
    gtk_list_box_append(GTK_LIST_BOX(apps_list_), row);
  }
}

void SukutWindow::RenderBuffering(GtkWidget* group) {
  if (group) adw_toggle_group_set_active_name(ADW_TOGGLE_GROUP(group), std::to_string(std::clamp(state_.buffering, 0, 2)).c_str());
}

void SukutWindow::RenderSettings() {
  adw_combo_row_set_selected(ADW_COMBO_ROW(language_row_), Ar() ? 1 : 0);
  adw_combo_row_set_selected(ADW_COMBO_ROW(theme_row_), state_.theme == "light" ? 1 : state_.theme == "dark" ? 2 : 0);
  // Without a tray, closing the window always exits Sukut.
  adw_switch_row_set_active(ADW_SWITCH_ROW(tray_row_), state_.close_to_tray && state_.tray_available);
  gtk_widget_set_sensitive(tray_row_, state_.tray_available);
  adw_action_row_set_subtitle(ADW_ACTION_ROW(tray_row_),
      state_.tray_available ? L("Closing the window keeps Sukut in the tray.", "إغلاق النافذة يُبقي سُكوت في منطقة الإشعارات.")
                            : L("No tray is available. On GNOME, install and enable the AppIndicator extension; until then, closing the window exits Sukut.",
                                "لا تتوفر منطقة إشعارات. على GNOME، ثبّت إضافة AppIndicator وفعّلها؛ وحتى ذلك الحين يخرج سُكوت عند إغلاق النافذة."));
  adw_switch_row_set_active(ADW_SWITCH_ROW(autostart_row_), state_.start_with_system);
  if (!static_cast<CommitState*>(g_object_get_data(G_OBJECT(amp_scale_), "commit"))->pressed)
    gtk_range_set_value(GTK_RANGE(amp_scale_), state_.amplification_db);
  gtk_label_set_text(GTK_LABEL(amp_value_), Gain(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(amp_scale_))))).c_str());
}

// ----- host commands -------------------------------------------------------

void SukutWindow::Send(std::vector<std::string> fields, std::function<void(bool, const std::string&)> done) {
  if (!link_) { if (done) done(true, ""); return; }
  link_->Send(std::move(fields), [done](bool ok, const std::string& payload) { if (done) done(ok, payload); });
}

void SukutWindow::Poll() {
  if (!link_ || polling_) return;
  if (!gtk_widget_get_mapped(window_) && have_state_) return;
  if (GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(window_)))
    if (have_state_ && (gdk_toplevel_get_state(GDK_TOPLEVEL(surface)) & GDK_TOPLEVEL_STATE_MINIMIZED)) return;
  polling_ = true;
  Send({"read"}, [this](bool ok, const std::string& payload) {
    polling_ = false;
    if (ok) Apply(HostState::From(Json::Parse(payload)));
  });
}

void SukutWindow::RefreshCatalog() {
  Send({"apps"}, [this](bool ok, const std::string& payload) {
    if (!ok) { catalog_.read = false; Render(); return; }
    const Catalog fresh = Catalog::From(Json::Parse(payload));
    if (!(fresh == catalog_) || !catalog_.read) Apply(fresh);
  });
}

void SukutWindow::Select(int mode) {
  if (command_ || !link_) return;
  command_ = true;
  error_.clear();
  Render();
  Send({"select", std::to_string(mode), state_.chosen_output, std::to_string(state_.source_pid), std::to_string(state_.buffering),
        std::to_string(state_.background_db)},
       [this](bool ok, const std::string& payload) {
         command_ = false;
         if (!ok) error_ = payload;
         Render();
         Poll();
       });
}

void SukutWindow::Save(const std::string& key, const std::string& value) {
  Send({"settings", key + "=" + value}, [this](bool ok, const std::string& error) {
    if (!ok) Toast(error);
    Poll();
  });
}

void SukutWindow::ChooseMode(int mode) {
  selected_mode_ = mode;
  // As on Windows beta.14: a new mode switches live while on; while off it is only remembered.
  if (state_.mode != 0) Select(mode);
  else { state_.preferred_mode = mode; Save("PreferredMode", std::to_string(mode)); }
  Render();
}

void SukutWindow::ChooseOutput(const std::string& name) {
  state_.chosen_output = name;
  if (state_.mode != 0) Select(state_.mode);
  else Save("Output", name);
}

void SukutWindow::ChooseApp(std::uint32_t pid) {
  state_.source_pid = pid;
  if (state_.mode != 0) Select(state_.mode);
  else Save("SourcePid", std::to_string(pid));
}

void SukutWindow::ChooseBuffering(int level) {
  state_.buffering = level;
  if (state_.mode != 0) Select(state_.mode);
  else Save("Buffering", std::to_string(level));
}

void SukutWindow::CommitBackground(int db) {
  if (db == state_.background_db) return;
  state_.background_db = db;
  if (state_.mode == 2) Select(2);
  else Save("OptimizedBackgroundDb", std::to_string(db));
}

void SukutWindow::CommitAmplification(int db) {
  if (db == state_.amplification_db) return;
  state_.amplification_db = db;
  Send({"amp", std::to_string(db)}, [this](bool ok, const std::string& error) { if (!ok) Toast(error); Poll(); });
}

void SukutWindow::SetLanguage(bool arabic) {
  if (arabic == Ar()) return;
  state_.language = arabic ? "ar" : "en";
  Save("Language", state_.language);
  // Rebuild after this signal returns: the row that sent it is part of the old build.
  g_idle_add([](gpointer self) -> gboolean {
    auto* s = static_cast<SukutWindow*>(self);
    s->Build();
    return G_SOURCE_REMOVE;
  }, this);
}

void SukutWindow::SetTheme(const std::string& theme) {
  state_.theme = theme;
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), theme == "light" ? ADW_COLOR_SCHEME_FORCE_LIGHT
                                                                      : theme == "dark" ? ADW_COLOR_SCHEME_FORCE_DARK
                                                                                        : ADW_COLOR_SCHEME_DEFAULT);
  Save("Theme", theme);
}

void SukutWindow::Toast(const std::string& text) {
  if (!text.empty()) adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toasts_), adw_toast_new(text.c_str()));
}

void SukutWindow::OpenSoundSettings() {
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  const bool plasma = desktop && std::string(desktop).find("KDE") != std::string::npos;
  GError* error = nullptr;
  if (!g_spawn_command_line_async(plasma ? "systemsettings kcm_pulseaudio" : "gnome-control-center sound", &error)) {
    Toast(L("Sound settings could not be opened.", "تعذّر فتح إعدادات الصوت."));
    if (error) g_error_free(error);
  }
}

void SukutWindow::CopyDiagnostics() {
  const auto& s = state_;
  std::string text = std::string("Sukut ") + SUKUT_VERSION + " (Linux)\n";
  text += "GTK " + std::to_string(gtk_get_major_version()) + "." + std::to_string(gtk_get_minor_version()) + "." +
          std::to_string(gtk_get_micro_version()) + ", libadwaita " + std::to_string(adw_get_major_version()) + "." +
          std::to_string(adw_get_minor_version()) + "." + std::to_string(adw_get_micro_version()) + "\n";
  text += "mode=" + std::to_string(s.mode) + " state=" + std::to_string(s.state) + " standby=" + std::to_string(s.standby) +
          " attention=" + s.attention + "\n";
  text += "output=" + s.output + " chosen=" + s.chosen_output + " sourcePid=" + std::to_string(s.source_pid) +
          " buffering=" + std::to_string(s.buffering) + "\n";
  text += "underruns=" + std::to_string(s.underruns) + " fallbacks=" + std::to_string(s.fallbacks) +
          " prioritySet=" + std::to_string(s.priority_set) + " trayAvailable=" + std::to_string(s.tray_available) + "\n";
  text += "outputs=" + std::to_string(catalog_.outputs.size()) + " apps=" + std::to_string(catalog_.apps.size()) + "\n";
  gdk_clipboard_set_text(gtk_widget_get_clipboard(window_), text.c_str());
  Toast(L("Diagnostics copied", "نُسخ التشخيص"));
}

}  // namespace sukut::window
