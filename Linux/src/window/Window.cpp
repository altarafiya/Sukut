#include "window/Window.h"
#include "window/AppIcon.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

#include <malloc.h>
#include <sys/wait.h>

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
  gtk_scale_set_digits(GTK_SCALE(scale), 0);
  gtk_range_set_increments(GTK_RANGE(scale), 1, 5);
  for (int db = static_cast<int>(min); db <= static_cast<int>(max); db += 5)
    gtk_scale_add_mark(GTK_SCALE(scale), db, GTK_POS_BOTTOM, nullptr);
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
    if (!gtk_window_is_active(GTK_WINDOW(w))) return;
    static_cast<SukutWindow*>(self)->RefreshSnapAccess();
    static_cast<SukutWindow*>(self)->RefreshCatalog();
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
  RefreshSnapAccess();
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
    if (s->banner_snap_) { s->OpenSnapAccessHelp(); return; }
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
    if (s->state_.mode == 0) s->RefreshSnapAccess();
    if (s->state_.mode == 0 && s->snap_audio_missing_) { s->OpenSnapAccessHelp(); return; }
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
    if (m == 2) {
      struct Hold {
        guint timer=0; double x=0,y=0; bool fired=false;
        GtkWidget* row; SukutWindow* owner;
        void Cancel(){if(timer){g_source_remove(timer);timer=0;}}
      };
      auto* hold=new Hold{0,0,0,false,row,this};
      g_object_set_data_full(G_OBJECT(row),"advanced-hold",hold,[](gpointer p){auto*h=static_cast<Hold*>(p);h->Cancel();delete h;});
      GtkEventController* controller=gtk_event_controller_legacy_new();
      gtk_event_controller_set_propagation_phase(controller,GTK_PHASE_CAPTURE);
      g_signal_connect(controller,"event",G_CALLBACK(+[](GtkEventControllerLegacy*,GdkEvent* event,gpointer p)->gboolean {
        auto*h=static_cast<Hold*>(p);const auto type=gdk_event_get_event_type(event);
        if(type==GDK_BUTTON_PRESS || type==GDK_TOUCH_BEGIN) {
          if(type==GDK_BUTTON_PRESS && gdk_button_event_get_button(event)!=GDK_BUTTON_PRIMARY)return FALSE;
          h->Cancel();h->fired=false;gdk_event_get_position(event,&h->x,&h->y);
          h->timer=g_timeout_add(h->owner->state_.advanced_optimized?3000:5000,[](gpointer pointer)->gboolean {
            auto*st=static_cast<Hold*>(pointer);st->timer=0;
            if(gtk_widget_get_mapped(st->row) && gtk_widget_is_sensitive(st->row)) {st->fired=true;if(st->owner->state_.advanced_optimized)st->owner->SetAdvancedMix(false);else st->owner->OpenAdvancedMix();}
            return G_SOURCE_REMOVE;
          },h);
        } else if(type==GDK_MOTION_NOTIFY || type==GDK_TOUCH_UPDATE) {
          double x=0;double y=0;gdk_event_get_position(event,&x,&y);
          if(std::abs(x-h->x)>10 || std::abs(y-h->y)>10)h->Cancel();
        } else if(type==GDK_BUTTON_RELEASE || type==GDK_TOUCH_END) {
          h->Cancel();const bool consumed=h->fired;h->fired=false;return consumed;
        } else if(type==GDK_TOUCH_CANCEL || type==GDK_LEAVE_NOTIFY || type==GDK_GRAB_BROKEN)h->Cancel();
        return FALSE;
      }),hold);
      gtk_widget_add_controller(row,controller);
      g_signal_connect(row,"unmap",G_CALLBACK(+[](GtkWidget*,gpointer p){static_cast<Hold*>(p)->Cancel();}),hold);
      GtkEventController* focus=gtk_event_controller_focus_new();
      g_signal_connect(focus,"leave",G_CALLBACK(+[](GtkEventControllerFocus*,gpointer p){static_cast<Hold*>(p)->Cancel();}),hold);
      gtk_widget_add_controller(row,focus);
    }
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
  advanced_mix_card_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top(advanced_mix_card_, 8);
  advanced_mix_values_={}; advanced_mix_scales_={};
  gtk_box_append(GTK_BOX(modes_card),advanced_mix_card_);
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

  // App icon: five marks on a rounded square of the chosen colour (window/AppIcon.h).
  GtkWidget* appearance = group(L("App icon", "أيقونة التطبيق"));
  if (app_icon::CanApply()) {
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(appearance),
        L("Used in the menu, the taskbar and this window. The tray keeps the س icon.",
          "تظهر في القائمة وشريط المهام وهذه النافذة. تبقى أيقونة س في منطقة الإشعارات."));
    GtkWidget* row = adw_preferences_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_focusable(row, FALSE);  // the choices inside take focus
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(box, 12);
    icon_marks_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(icon_marks_), TRUE);
    icon_colors_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), icon_marks_);
    gtk_box_append(GTK_BOX(box), Label(L("Background colour", "لون الخلفية"), "dim-label"));
    gtk_box_append(GTK_BOX(box), icon_colors_);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), row);
    RenderAppIcon();
  } else {
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(appearance),
        L("In the Flatpak and Snap versions the desktop shows the standard icon; install the .deb to choose another.",
          "في نسختي Flatpak وSnap يعرض سطح المكتب الأيقونة الافتراضية؛ ثبّت حزمة .deb لاختيار أيقونة أخرى."));
  }

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

// A snap can't grant itself these connections, so until the Snap Store connects
// them automatically the user runs one command. snapctl exits 1 when not connected.
void SukutWindow::RefreshSnapAccess() {
  if (std::getenv("SNAP") == nullptr) return;
  const bool had_audio_missing = snap_audio_missing_;
  const std::string before = snap_command_;
  std::string command;
  bool audio_missing = false;
  for (const char* plug : {"pipewire", "audio-record", "raw-usb"}) {
    gint status = 0;
    const std::string line = std::string("snapctl is-connected ") + plug;
    if (!g_spawn_command_line_sync(line.c_str(), nullptr, nullptr, &status, nullptr)) continue;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 1) continue;
    command += (command.empty() ? "" : " && ") + std::string("sudo snap connect sukut:") + plug;
    if (std::string(plug) != "raw-usb") audio_missing = true;
  }
  snap_command_ = command;
  snap_audio_missing_ = audio_missing;
  if (before == snap_command_) return;
  if (had_audio_missing && !audio_missing && link_) Send({"refresh"}, [this](bool, const std::string&) { RefreshCatalog(); Poll(); });
  Render();
}

// The command, selectable, with a Copy button.
GtkWidget* SukutWindow::SnapCommandBox(GtkWidget** label_out) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* label = gtk_label_new(snap_command_.c_str());
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  gtk_widget_set_direction(label, GTK_TEXT_DIR_LTR);
  gtk_widget_add_css_class(label, "monospace");
  gtk_box_append(GTK_BOX(box), label);
  GtkWidget* copy = gtk_button_new_with_label(L("Copy command", "نسخ الأمر"));
  gtk_widget_add_css_class(copy, "suggested-action");
  gtk_widget_set_halign(copy, GTK_ALIGN_START);
  Connect(copy, "clicked", +[](GtkButton* b, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(b)), s->snap_command_.c_str());
    gtk_button_set_label(b, s->L("Copied", "تم النسخ"));
  }, this);
  gtk_box_append(GTK_BOX(box), copy);
  if (label_out) *label_out = label;
  return box;
}

void SukutWindow::OpenSnapAccessHelp() {
  AdwDialog* dialog = adw_alert_dialog_new(
      L("Allow Sukut to use your audio", "اسمح لسُكوت باستخدام الصوت"),
      L("Snap apps can't give themselves this permission. Open Terminal, paste this command and press Enter. You only need to do this once.",
        "لا تستطيع تطبيقات Snap منح نفسها هذا الإذن. افتح الطرفية، والصق هذا الأمر، ثم اضغط Enter. تحتاج إلى ذلك مرة واحدة فقط."));
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), SnapCommandBox(nullptr));
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
  const bool raise = !first && state.raise_requests != state_.raise_requests;
  state_ = state;
  have_state_ = true;
  if (state_.mode != 0) selected_mode_ = state_.mode;
  else if (!command_) selected_mode_ = state_.preferred_mode;
  AdwStyleManager* manager = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(manager, state_.theme == "light" ? ADW_COLOR_SCHEME_FORCE_LIGHT
                                              : state_.theme == "dark" ? ADW_COLOR_SCHEME_FORCE_DARK
                                                                       : ADW_COLOR_SCHEME_DEFAULT);
  if (language_changed || built_arabic_ != Ar()) Build();
  if (raise) Present();
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
  banner_snap_ = snap_audio_missing_;
  if (banner_snap_) banner = L("Sukut needs your permission to use audio.", "يحتاج سُكوت إلى إذنك لاستخدام الصوت.");
  else if (!error_.empty()) { banner = ErrorText(error_); restart = error_ != "source app closed"; }
  else if (state_.state == 2) { banner = ErrorText(state_.attention); restart = state_.attention != "source_closed"; }
  else if (state_.mode != 0 && !state_.notice.empty()) { banner = NoticeText(state_.notice); banner_notice_ = !banner.empty(); }
  adw_banner_set_title(ADW_BANNER(banner_), banner.c_str());
  adw_banner_set_button_label(ADW_BANNER(banner_), banner_snap_ ? L("Allow access", "السماح بالوصول")
                                                   : banner_notice_ ? L("How to fix", "طريقة الإصلاح")
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

void SukutWindow::EnsureAdvancedMixControls() {
  if(advanced_mix_scales_[0])return;
  const char* mix_icons[] = {"audio-input-microphone-symbolic", "multimedia-player-symbolic", "audio-speakers-symbolic"};
  const char* mix_tones[] = {"accent-text", "mode-3", "mode-2"};
  for (int stem=0; stem<3; ++stem) {
    const char* name=stem==0?L("Voice", "الصوت البشري"):stem==1?L("Music", "الموسيقى"):L("Background", "الخلفية");
    GtkWidget* header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_box_append(GTK_BOX(header),Icon(mix_icons[stem],mix_tones[stem]));
    GtkWidget* label=Label(name,"section-heading",false); gtk_widget_set_hexpand(label,TRUE);
    gtk_widget_add_css_class(label,mix_tones[stem]); gtk_box_append(GTK_BOX(header),label);
    advanced_mix_values_[stem]=Label("",mix_tones[stem],false);
    gtk_widget_set_direction(advanced_mix_values_[stem],GTK_TEXT_DIR_LTR);
    gtk_box_append(GTK_BOX(header),advanced_mix_values_[stem]);
    gtk_box_append(GTK_BOX(advanced_mix_card_),header);
    advanced_mix_scales_[stem]=CommitScale(-20,10,&applying_,
      [this,stem](int db){gtk_label_set_text(GTK_LABEL(advanced_mix_values_[stem]),Gain(db).c_str());},
      [this,stem](int db){CommitAdvancedGain(stem,db);});
    gtk_widget_add_css_class(advanced_mix_scales_[stem],mix_tones[stem]); Name(advanced_mix_scales_[stem],name);
    gtk_box_append(GTK_BOX(advanced_mix_card_),advanced_mix_scales_[stem]);
  }
}

void SukutWindow::VerifyMixSlidersForTest() {
  g_assert_null(link_);
  EnsureAdvancedMixControls();
  const HostState stale = state_;
  for (int stem=0; stem<3; ++stem) {
    auto* range=GTK_RANGE(advanced_mix_scales_[stem]);
    auto* adjustment=gtk_range_get_adjustment(range);
    g_assert_cmpfloat(gtk_adjustment_get_lower(adjustment),==,-20);
    g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment),==,10);
    g_assert_cmpfloat(gtk_adjustment_get_step_increment(adjustment),==,1);
    auto* interaction=static_cast<CommitState*>(g_object_get_data(G_OBJECT(range),"commit"));
    interaction->pressed=true;
    gtk_range_set_value(range,-14);
    Apply(stale); // periodic host refresh during a live drag
    g_assert_cmpfloat(gtk_range_get_value(range),==,-14);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(advanced_mix_values_[stem])),==,"-14 dB");
    gtk_range_set_value(range,-20);
    Apply(stale);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(advanced_mix_values_[stem])),==,"-20 dB");
    interaction->pressed=false;
  }
  Render();
  g_print("mix_slider_refresh=PASS range=-20..10 step=1 live_label=-14dB minimum_is_not_mute\n");
}

void SukutWindow::RenderModes() {
  for (int m = 1; m <= 3; ++m) gtk_check_button_set_active(GTK_CHECK_BUTTON(mode_checks_[m - 1]), selected_mode_ == m);
  gtk_widget_set_visible(background_card_, selected_mode_ == 2 && !state_.advanced_optimized);
  gtk_widget_set_visible(advanced_mix_card_, selected_mode_ == 2 && state_.advanced_optimized);
  if(state_.advanced_optimized)EnsureAdvancedMixControls();
  const int gains[]={state_.advanced_voice_db,state_.advanced_music_db,state_.advanced_background_db};
  for(int stem=0;stem<3 && advanced_mix_scales_[0];++stem) {
    const auto* interaction = static_cast<CommitState*>(g_object_get_data(G_OBJECT(advanced_mix_scales_[stem]),"commit"));
    // A host snapshot can arrive while dragging or during a keyboard commit.
    // Keep both the thumb and its label on the live value until that interaction ends.
    if (!interaction->pressed && !interaction->pause)
      gtk_range_set_value(GTK_RANGE(advanced_mix_scales_[stem]),std::clamp(gains[stem],-20,10));
    const int shown = static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(advanced_mix_scales_[stem]))));
    gtk_label_set_text(GTK_LABEL(advanced_mix_values_[stem]),Gain(shown).c_str());
  }
  if (const auto* interaction = static_cast<CommitState*>(g_object_get_data(G_OBJECT(background_scale_), "commit")); !interaction->pressed && !interaction->pause)
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
    if (have_state_ && std::getenv("SNAP") == nullptr && (gdk_toplevel_get_state(GDK_TOPLEVEL(surface)) & GDK_TOPLEVEL_STATE_MINIMIZED)) return;
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
        std::to_string(state_.background_db),state_.advanced_optimized?"1":"0",std::to_string(state_.advanced_voice_db),
        std::to_string(state_.advanced_music_db),std::to_string(state_.advanced_background_db)},
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

void SukutWindow::CommitAdvancedGain(int stem, int db) {
  db=std::clamp(db,-20,10);
  int& saved=stem==0?state_.advanced_voice_db:stem==1?state_.advanced_music_db:state_.advanced_background_db;
  if(saved==db)return;
  saved=db;
  if(state_.mode==2)Select(2);
  else Save(stem==0?"AdvancedVoiceDb":stem==1?"AdvancedMusicDb":"AdvancedBackgroundDb",std::to_string(db));
}
void SukutWindow::SetAdvancedMix(bool enabled) {
  if(state_.advanced_optimized==enabled)return;
  // Advanced mode starts music at its lowest finite gain; standard mode still mutes it.
  if(enabled) { state_.advanced_voice_db=0;state_.advanced_music_db=-20;state_.advanced_background_db=state_.background_db; }
  state_.advanced_optimized=enabled;
  Send({"settings",std::string("AdvancedOptimized=")+(enabled?"1":"0"),"AdvancedVoiceDb="+std::to_string(state_.advanced_voice_db),
      "AdvancedMusicDb="+std::to_string(state_.advanced_music_db),"AdvancedBackgroundDb="+std::to_string(state_.advanced_background_db)},
      [this,enabled](bool ok,const std::string& error){
        if(!ok){state_.advanced_optimized=!enabled;Toast(error);Render();return;}
        selected_mode_=2;
        if(state_.mode!=0)Select(2);else {Save("PreferredMode","2");Render();}
      });
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


namespace {
// A cairo image (premultiplied ARGB, native order) as a GdkTexture for GtkPicture.
GdkTexture* Texture(cairo_surface_t* surface) {
  cairo_surface_flush(surface);
  const int w = cairo_image_surface_get_width(surface), h = cairo_image_surface_get_height(surface);
  const int stride = cairo_image_surface_get_stride(surface);
  GBytes* bytes = g_bytes_new(cairo_image_surface_get_data(surface), static_cast<gsize>(stride) * h);
  GdkTexture* texture = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, stride);
  g_bytes_unref(bytes);
  return texture;
}
}  // namespace

void SukutWindow::RenderAppIcon() {
  if (!icon_marks_) return;
  const app_icon::Choice current = app_icon::Load();
  for (GtkWidget* box : {icon_marks_, icon_colors_})
    while (GtkWidget* child = gtk_widget_get_first_child(box)) gtk_box_remove(GTK_BOX(box), child);
  auto choose = [](GtkWidget* button, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    app_icon::Choice next = app_icon::Load();
    if (const char* mark = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "mark"))) next.mark = mark;
    if (const char* color = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "color"))) next.color = color;
    std::string error;
    if (!app_icon::Apply(next, error)) s->Toast(error);
    else s->Toast(s->L("App icon changed. Some desktops show it after you sign in again.",
                       "تغيّرت أيقونة التطبيق. تظهر في بعض أسطح المكتب بعد تسجيل الدخول مجددًا."));
    // Rebuild after the click has finished.
    g_idle_add_once(+[](gpointer data) { static_cast<SukutWindow*>(data)->RenderAppIcon(); }, s);
  };
  for (const auto& mark : app_icon::kMarks) {
    GtkWidget* button = gtk_toggle_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "icon-choice");
    cairo_surface_t* image = app_icon::Render({mark.id, current.color}, 112);
    GdkTexture* texture = Texture(image);
    GtkWidget* picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_unref(texture);
    cairo_surface_destroy(image);
    gtk_widget_set_size_request(picture, 56, 56);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_button_set_child(GTK_BUTTON(button), picture);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), current.mark == mark.id);
    Name(button, L(mark.en, mark.ar));
    g_object_set_data(G_OBJECT(button), "mark", const_cast<char*>(mark.id));
    g_signal_connect(button, "clicked", G_CALLBACK(+choose), this);
    gtk_box_append(GTK_BOX(icon_marks_), button);
  }
  for (const auto& color : app_icon::kColors) {
    GtkWidget* button = gtk_toggle_button_new();
    AddClasses(button, {"circular", "color-choice"});
    gtk_widget_set_size_request(button, 34, 34);
    GtkCssProvider* fill = gtk_css_provider_new();
    gtk_css_provider_load_from_string(fill, (std::string("button { background: ") + color.hex + "; }").c_str());
    gtk_style_context_add_provider(gtk_widget_get_style_context(button), GTK_STYLE_PROVIDER(fill), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(fill);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), current.color == color.id);
    Name(button, L(color.en, color.ar));
    g_object_set_data(G_OBJECT(button), "color", const_cast<char*>(color.id));
    g_signal_connect(button, "clicked", G_CALLBACK(+choose), this);
    gtk_box_append(GTK_BOX(icon_colors_), button);
  }
}

}  // namespace sukut::window
