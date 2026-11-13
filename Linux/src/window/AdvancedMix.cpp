#include "window/Window.h"
#include "window/AdvancedMixAccess.h"
#include "window/Widgets.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sukut::window {
namespace {
std::filesystem::path AccessPath() { return std::filesystem::path(g_get_user_config_dir()) / "sukut" / "advanced-mix-access"; }
AdvancedMixAccess LoadAccess() {
  AdvancedMixAccess state;
  std::ifstream in(AccessPath()); in >> state.failures >> state.locked_until;
  state.failures = std::clamp(state.failures, 0, 2);
  return state;
}
bool SaveAccess(const AdvancedMixAccess& state) {
  const auto path = AccessPath();
  g_mkdir_with_parents(path.parent_path().c_str(), 0700);
  const std::string text = std::to_string(state.failures) + " " + std::to_string(state.locked_until) + "\n";
  return g_file_set_contents(path.c_str(), text.c_str(), text.size(), nullptr);
}
struct DialogState {
  AdwDialog* dialog;
  GtkWidget* input;
  GtkWidget* error;
  bool arabic;
  std::function<void(bool)> enable;
  std::function<void(const std::string&)> toast;
};
}  // namespace

void SukutWindow::OpenAdvancedMix() {
  if(state_.advanced_optimized){SetAdvancedMix(false);return;}
  if (advanced_mix_dialog_) { adw_dialog_present(ADW_DIALOG(advanced_mix_dialog_), window_); return; }
  const auto state = LoadAccess();
  const auto now = g_get_real_time() / 1000;
  if (state.locked_until > now) {
    Toast(std::string(L("Try again in ", "أعد المحاولة بعد ")) + std::to_string((state.locked_until - now + 59999) / 60000) + L(" minutes.", " دقائق."));
    return;
  }
  AdwDialog* dialog = adw_dialog_new(); advanced_mix_dialog_ = GTK_WIDGET(dialog);
  adw_dialog_set_title(dialog, L("Advanced Optimized mix", "المزج المتقدم للوضع المحسّن"));
  adw_dialog_set_content_width(dialog, 420);
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(content, 24);
  GtkWidget* icon = Icon("io.github.altarafiya.Sukut-sliders-symbolic", "accent-text");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 36); gtk_widget_set_halign(icon, GTK_ALIGN_START); gtk_box_append(GTK_BOX(content), icon);
  gtk_box_append(GTK_BOX(content), Label(L("Advanced Optimized mix", "المزج المتقدم للوضع المحسّن"), "title-2"));
  gtk_box_append(GTK_BOX(content), Label(L("Control Voice, Music and Background in 1 dB steps, from −20 to +10 dB. The lowest step reduces sound; it does not mute it.",
      "تحكّم في الصوت البشري والموسيقى والخلفية بخطوات قدرها 1 dB، من ‎−20 إلى ‎+10 dB. أقل قيمة تخفّض الصوت ولا تكتمه."), "dim-label"));
  GtkWidget* input = nullptr;
  GtkWidget* error = Label("", "error");
  if (!state_.advanced_optimized) {
    input = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(input), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(input), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_placeholder_text(GTK_ENTRY(input), L("Enter the access code", "أدخل رمز الوصول"));
    gtk_widget_set_direction(input, GTK_TEXT_DIR_LTR);
    gtk_box_append(GTK_BOX(content), input); gtk_box_append(GTK_BOX(content), error);
  }
  GtkWidget* enable = gtk_button_new_with_label(state_.advanced_optimized ? L("Use standard Optimized", "استخدام الوضع المحسّن العادي") : L("Enable advanced mode", "تفعيل الوضع المتقدم"));
  gtk_widget_add_css_class(enable, "suggested-action"); gtk_widget_add_css_class(enable, "pill");
  GtkWidget* cancel = gtk_button_new_with_label(L("Cancel", "إلغاء")); gtk_widget_add_css_class(cancel, "pill");
  gtk_box_append(GTK_BOX(content), enable); gtk_box_append(GTK_BOX(content), cancel);
  auto* data = new DialogState{dialog, input, error, Ar(), [this](bool on) { SetAdvancedMix(on); }, [this](const std::string& text) { Toast(text); }};
  g_object_set_data_full(G_OBJECT(dialog), "access", data, [](gpointer p) { delete static_cast<DialogState*>(p); });
  g_signal_connect(enable, "clicked", G_CALLBACK(+[](GtkButton*, gpointer pointer) {
    auto* d = static_cast<DialogState*>(pointer);
    if (!d->input) { d->enable(false); adw_dialog_close(d->dialog); return; }
    auto access = LoadAccess();
    const auto result = access.Attempt(gtk_editable_get_text(GTK_EDITABLE(d->input)), g_get_real_time() / 1000);
    if(!SaveAccess(access)) {gtk_label_set_text(GTK_LABEL(d->error),T(d->arabic,"Could not save access settings. Try again later.","تعذّر حفظ إعدادات الوصول. أعد المحاولة لاحقًا."));return;}
    gtk_editable_set_text(GTK_EDITABLE(d->input), "");
    if (result == AdvancedMixAccess::Result::Enabled) { d->enable(true); adw_dialog_close(d->dialog); }
    else if (result == AdvancedMixAccess::Result::Locked) {
      d->toast(T(d->arabic, "Try again in 10 minutes.", "أعد المحاولة بعد 10 دقائق.")); adw_dialog_close(d->dialog);
    } else {
      const std::string text = std::string(T(d->arabic, "Incorrect code. Attempts remaining: ", "رمز غير صحيح. المحاولات المتبقية: ")) + std::to_string(3-access.failures);
      gtk_label_set_text(GTK_LABEL(d->error), text.c_str()); gtk_widget_grab_focus(d->input);
    }
  }), data);
  g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer dialog) { adw_dialog_close(ADW_DIALOG(dialog)); }), dialog);
  g_signal_connect(dialog, "closed", G_CALLBACK(+[](AdwDialog*, gpointer pointer) { static_cast<SukutWindow*>(pointer)->advanced_mix_dialog_ = nullptr; }), this);
  adw_dialog_set_child(dialog, content); adw_dialog_present(dialog, window_);
  if (input) gtk_widget_grab_focus(input);
}
}  // namespace sukut::window
