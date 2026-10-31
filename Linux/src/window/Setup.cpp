// The first-run setup guide (Linux/PLAN.md section 7): Welcome, System check,
// Output and Ready. It owns the whole window until it finishes, as on Windows.
#include <algorithm>
#include <string>

#include "window/Window.h"
#include "window/Widgets.h"

namespace sukut::window {
namespace {

constexpr int kSteps = 4;

GtkWidget* Card() {
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  AddClasses(card, {"card", "setup-card"});
  return card;
}

GtkWidget* Line(const char* icon_name, const char* tone, const std::string& text) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget* icon = Icon(icon_name, tone);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
  gtk_widget_set_valign(icon, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(row), icon);
  GtkWidget* label = Label(text);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(row), label);
  return row;
}

GtkWidget* PageIcon(const char* icon_name) {
  GtkWidget* icon = Icon(icon_name, "accent-text");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
  gtk_widget_set_halign(icon, GTK_ALIGN_START);
  return icon;
}

GtkWidget* StepPage(GtkWidget* stack, int step) {
  GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_stack_add_named(GTK_STACK(stack), page, ("step-" + std::to_string(step)).c_str());
  return page;
}

}  // namespace

GtkWidget* SukutWindow::BuildSetup() {
  GtkWidget* view = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("سُكوت · Sukut", L("Setup guide", "دليل الإعداد")));
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  GtkWidget* clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 560);
  GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(body, 24);
  setup_step_label_ = Label("", "dim-label", false);
  gtk_widget_add_css_class(setup_step_label_, "caption");
  setup_progress_ = gtk_progress_bar_new();
  GtkWidget* progress = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(progress), setup_step_label_);
  gtk_box_append(GTK_BOX(progress), setup_progress_);
  gtk_box_append(GTK_BOX(body), progress);
  setup_stack_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(setup_stack_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_vhomogeneous(GTK_STACK(setup_stack_), FALSE);
  gtk_box_append(GTK_BOX(body), setup_stack_);

  // 1. Welcome.
  GtkWidget* welcome = StepPage(setup_stack_, 0);
  GtkWidget* logo = gtk_image_new_from_icon_name("io.github.altarafiya.Sukut");
  gtk_image_set_pixel_size(GTK_IMAGE(logo), 72);
  gtk_widget_set_halign(logo, GTK_ALIGN_START);
  gtk_accessible_update_property(GTK_ACCESSIBLE(logo), GTK_ACCESSIBLE_PROPERTY_LABEL, "Sukut", -1);
  gtk_box_append(GTK_BOX(welcome), logo);
  gtk_box_append(GTK_BOX(welcome), Label(L("Welcome to Sukut", "أهلًا بك في سُكوت"), "setup-title"));
  gtk_box_append(GTK_BOX(welcome), Label(L("Enjoy the internet. Without the music.", "استمتع بالإنترنت، بلا موسيقى."), "dim-label"));
  GtkWidget* language = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(language), Label(L("Language", "اللغة"), "heading", false));
  const char* languages[] = {"English", "العربية", nullptr};
  setup_language_ = gtk_drop_down_new_from_strings(languages);
  gtk_widget_set_halign(setup_language_, GTK_ALIGN_START);
  gtk_widget_set_size_request(setup_language_, 240, -1);
  Name(setup_language_, L("Language", "اللغة"));
  gtk_widget_set_has_tooltip(setup_language_, FALSE);
  g_signal_connect(setup_language_, "notify::selected", G_CALLBACK(+[](GObject* drop, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (!s->applying_) s->SetLanguage(gtk_drop_down_get_selected(GTK_DROP_DOWN(drop)) == 1);
  }), this);
  gtk_box_append(GTK_BOX(language), setup_language_);
  gtk_box_append(GTK_BOX(welcome), language);
  GtkWidget* overview = Card();
  gtk_box_append(GTK_BOX(overview), Line("audio-card-symbolic", "mode-2",
      L("Sukut takes your apps' sound through its own PipeWire output, with no driver to install.",
        "يمرّر سُكوت صوت تطبيقاتك عبر مخرجه الخاص في PipeWire، دون تثبيت برنامج تشغيل.")));
  gtk_box_append(GTK_BOX(overview), Line("audio-headphones-symbolic", "ready-text", L("Choose your speakers or headphones", "اختر السماعات أو مكبر الصوت")));
  gtk_box_append(GTK_BOX(overview), Line("audio-speakers-symbolic", "accent-text", L("Then choose a mode and press Start", "ثم اختر وضعًا واضغط «تشغيل»")));
  gtk_box_append(GTK_BOX(welcome), overview);

  // 2. System check: PipeWire and the tray.
  GtkWidget* check = StepPage(setup_stack_, 1);
  gtk_box_append(GTK_BOX(check), PageIcon("computer-symbolic"));
  gtk_box_append(GTK_BOX(check), Label(L("Check your system", "فحص النظام"), "setup-title"));
  gtk_box_append(GTK_BOX(check), Label(L("Sukut uses PipeWire, the sound system of current Linux desktops, and the tray for when the window is closed.",
                                         "يستخدم سُكوت PipeWire، نظام الصوت في أنظمة لينكس الحديثة، ومنطقة الإشعارات عند إغلاق النافذة."),
                                       "dim-label"));
  GtkWidget* status = Card();
  auto status_line = [&](GtkWidget** dot, GtkWidget** text) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(*dot, "dot");
    gtk_widget_set_valign(*dot, GTK_ALIGN_START);
    gtk_widget_set_margin_top(*dot, 6);
    gtk_box_append(GTK_BOX(row), *dot);
    *text = Label("");
    gtk_widget_set_hexpand(*text, TRUE);
    gtk_box_append(GTK_BOX(row), *text);
    gtk_box_append(GTK_BOX(status), row);
  };
  status_line(&setup_audio_dot_, &setup_audio_text_);
  status_line(&setup_tray_dot_, &setup_tray_text_);
  gtk_box_append(GTK_BOX(check), status);
  GtkWidget* again = gtk_button_new_with_label(L("Check again", "إعادة الفحص"));
  gtk_widget_set_halign(again, GTK_ALIGN_START);
  g_signal_connect(again, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    s->Send({"refresh"}, [s](bool, const std::string&) { s->RefreshCatalog(); s->Poll(); });
  }), this);
  gtk_box_append(GTK_BOX(check), again);

  // 3. Output and buffering.
  GtkWidget* output = StepPage(setup_stack_, 2);
  gtk_box_append(GTK_BOX(output), PageIcon("audio-headphones-symbolic"));
  gtk_box_append(GTK_BOX(output), Label(L("Choose your audio output", "اختر مخرج الصوت"), "setup-title"));
  gtk_box_append(GTK_BOX(output), Label(L("Pick the speakers or headphones you listen through.", "اختر السماعات أو مكبر الصوت الذي تستمع من خلاله."), "dim-label"));
  gtk_box_append(GTK_BOX(output), BuildOutputList(&setup_outputs_));
  gtk_box_append(GTK_BOX(output), Label(L("You can change this later on the Audio page.", "يمكنك تغييره لاحقًا من صفحة الصوت."), "dim-label"));
  GtkWidget* physical = Card();
  gtk_box_append(GTK_BOX(physical), Line("object-select-symbolic", "ready-text",
      L("Choose your real headphones or speakers here. Sukut routes your apps through its own output automatically when you press Start.",
        "اختر سماعاتك الفعلية هنا. يوجّه سُكوت صوت تطبيقاتك عبر مخرجه تلقائيًا عند الضغط على «تشغيل».")));
  gtk_box_append(GTK_BOX(output), physical);
  gtk_box_append(GTK_BOX(output), Label(L("Audio buffering", "التخزين المؤقت للصوت"), "heading", false));
  gtk_box_append(GTK_BOX(output), BuildBuffering(&setup_buffering_));

  // 4. Ready.
  GtkWidget* ready = StepPage(setup_stack_, 3);
  gtk_box_append(GTK_BOX(ready), Label(L("You’re ready", "أنت جاهز"), "setup-title"));
  gtk_box_append(GTK_BOX(ready), Label(L("Choose a mode on the Audio page, then press Start. Sukut switches your apps' sound automatically.",
                                         "اختر وضعًا من صفحة الصوت، ثم اضغط «تشغيل». يحوّل سُكوت صوت تطبيقاتك تلقائيًا."),
                                       "dim-label"));
  GtkWidget* hints = Card();
  gtk_box_append(GTK_BOX(hints), Line("io.github.altarafiya.Sukut-music-filter-symbolic", "mode-1",
      L("Music Filter — removes music and improves speech clarity.", "مرشّح الموسيقى — يزيل الموسيقى ويحسّن وضوح الكلام.")));
  gtk_box_append(GTK_BOX(hints), Line("changes-prevent-symbolic", "mode-3",
      L("Strict — the strongest music removal, muting music and background. Some sound effects may be removed too.",
        "صارم — أقوى إزالة للموسيقى مع كتم الموسيقى والخلفية. قد تختفي بعض المؤثرات الصوتية أيضًا.")));
  gtk_box_append(GTK_BOX(hints), Line("io.github.altarafiya.Sukut-sliders-symbolic", "mode-2",
      L("Optimized — mutes music. Adjust background sounds with its slider on the Audio page.",
        "محسّن — يكتم الموسيقى. اضبط أصوات الخلفية من شريطه في صفحة الصوت.")));
  gtk_box_append(GTK_BOX(hints), Line("system-shutdown-symbolic", "mode-off",
      L("Stop — stops processing and gives your audio back to your output.", "إيقاف — يوقف المعالجة ويعيد الصوت إلى جهاز التشغيل المختار.")));
  gtk_box_append(GTK_BOX(ready), hints);
  setup_tray_hint_ = Label("", "dim-label");
  gtk_box_append(GTK_BOX(ready), setup_tray_hint_);
  GtkWidget* prayer_card = Card();
  GtkWidget* prayer = gtk_label_new("ادعوا لخالي بالرَّحمة، ولزوجته بالشِّفاء،\nولجميع المسلمين.");
  gtk_label_set_justify(GTK_LABEL(prayer), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap(GTK_LABEL(prayer), TRUE);
  gtk_widget_set_direction(prayer, GTK_TEXT_DIR_RTL);
  gtk_widget_add_css_class(prayer, "prayer");
  gtk_box_append(GTK_BOX(prayer_card), prayer);
  gtk_box_append(GTK_BOX(ready), prayer_card);

  adw_clamp_set_child(ADW_CLAMP(clamp), body);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);

  GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(buttons, 12);
  setup_back_ = gtk_button_new_with_label(L("Back", "السابق"));
  g_signal_connect(setup_back_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->setup_step_ > 0) s->ShowSetup(s->setup_step_ - 1);
  }), this);
  GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(spacer, TRUE);
  setup_next_ = gtk_button_new_with_label("");
  gtk_widget_add_css_class(setup_next_, "suggested-action");
  g_signal_connect(setup_next_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->setup_step_ < kSteps - 1) { s->ShowSetup(s->setup_step_ + 1); return; }
    s->state_.setup_completed = true;
    s->Save("SetupCompleted", "1");
    s->HideSetup();
  }), this);
  gtk_box_append(GTK_BOX(buttons), setup_back_);
  gtk_box_append(GTK_BOX(buttons), spacer);
  gtk_box_append(GTK_BOX(buttons), setup_next_);
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(view), buttons);
  return view;
}

void SukutWindow::ShowSetup(int step) {
  if (!setup_stack_) gtk_stack_add_named(GTK_STACK(root_stack_), BuildSetup(), "setup");
  setup_step_ = std::clamp(step, 0, kSteps - 1);
  gtk_stack_set_visible_child_name(GTK_STACK(root_stack_), "setup");
  gtk_stack_set_visible_child_name(GTK_STACK(setup_stack_), ("step-" + std::to_string(setup_step_)).c_str());
  if (setup_step_ == 1 || setup_step_ == 2) RefreshCatalog();
  Render();
  gtk_widget_grab_focus(setup_next_);
}

void SukutWindow::HideSetup() {
  setup_step_ = -1;
  gtk_stack_set_visible_child_name(GTK_STACK(root_stack_), "main");
}

void SukutWindow::RenderSetup() {
  if (!setup_stack_ || setup_step_ < 0) return;
  const int current = setup_step_ + 1;
  const std::string step = Ar() ? "الخطوة " + std::to_string(current) + " من " + std::to_string(kSteps)
                                : "Step " + std::to_string(current) + " of " + std::to_string(kSteps);
  gtk_label_set_text(GTK_LABEL(setup_step_label_), step.c_str());
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(setup_progress_), static_cast<double>(current) / kSteps);
  gtk_widget_set_sensitive(setup_back_, setup_step_ > 0);
  gtk_button_set_label(GTK_BUTTON(setup_next_), setup_step_ == kSteps - 1 ? L("Open Sukut", "فتح سُكوت") : L("Next", "التالي"));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(setup_language_), Ar() ? 1 : 0);

  const bool audio = catalog_.read;
  SetClass(setup_audio_dot_, "ready", audio);
  SetClass(setup_audio_dot_, "missing", !audio);
  gtk_label_set_text(GTK_LABEL(setup_audio_text_),
      audio ? L("PipeWire and WirePlumber are running.", "يعمل PipeWire وWirePlumber.")
            : L("PipeWire is not answering. Install pipewire and wireplumber, then sign in again.",
                "لا يستجيب PipeWire. ثبّت pipewire وwireplumber ثم سجّل الدخول من جديد."));
  SetClass(setup_tray_dot_, "ready", state_.tray_available);
  SetClass(setup_tray_dot_, "missing", !state_.tray_available);
  gtk_label_set_text(GTK_LABEL(setup_tray_text_),
      state_.tray_available
          ? L("The tray is available. Closing the window keeps Sukut there.", "منطقة الإشعارات متاحة. يبقى سُكوت فيها عند إغلاق النافذة.")
          : L("No tray found. On GNOME, install the AppIndicator extension (gnome-shell-extension-appindicator) and turn it on in Extensions. Until then, closing the window exits Sukut.",
              "لم يُعثر على منطقة إشعارات. على GNOME، ثبّت إضافة AppIndicator ‏(gnome-shell-extension-appindicator) وفعّلها من تطبيق الإضافات. وحتى ذلك الحين يخرج سُكوت عند إغلاق النافذة."));
  RenderOutputs(setup_outputs_);
  RenderBuffering(setup_buffering_);
  gtk_label_set_text(GTK_LABEL(setup_tray_hint_),
      state_.close_to_tray && state_.tray_available
          ? L("Closing keeps Sukut in the tray. Use its menu to open, stop, or exit.", "يبقى سُكوت في منطقة الإشعارات عند الإغلاق. من قائمته يمكنك الفتح أو الإيقاف أو الخروج.")
          : L("Closing exits Sukut and restores normal audio.", "يخرج إغلاق النافذة من سُكوت ويعيد الصوت العادي."));
}

}  // namespace sukut::window
