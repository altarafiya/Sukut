// The Phone page (Linux/PLAN.md section 7): the Windows flows from
// PhoneSetup.cs and PhoneConnectionCard.cs. Choices, the seven-step Full setup,
// Activate, Install or update, USB help, and the connection card at the bottom.
// adb work runs on a background thread; results come back on the main loop.
#include <algorithm>
#include <thread>
#include <utility>

#include "audio/Sandbox.h"
#include "window/Style.h"
#include "window/Widgets.h"
#include "window/Window.h"

namespace sukut::window {
namespace {

constexpr int kPhoneSteps = 5;

void Post(std::function<void()> task) {
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()>*>(data))();
        return G_SOURCE_REMOVE;
      },
      new std::function<void()>(std::move(task)), [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}

// Latin commands inside Arabic text stay one left-to-right run.
std::string Isolate(const std::string& text) { return "\xE2\x81\xA6" + text + "\xE2\x81\xA9"; }

struct Bullet {
  const char* icon;
  const char* tone;
  std::string title, detail;
};

GtkWidget* Heading(const std::string& text) {
  GtkWidget* label = Label(text, "setup-title");
  gtk_widget_set_margin_bottom(label, 4);
  return label;
}

GtkWidget* Bullets(std::initializer_list<Bullet> items) {
  GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  for (const auto& item : items) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* icon = Icon(item.icon, item.tone);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), icon);
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget* title = Label(item.title, "help-title");
    gtk_widget_add_css_class(title, item.tone);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), Label(item.detail, "dim-label"));
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(list), row);
  }
  return list;
}

GtkWidget* Note(const char* tone, const std::string& text) {
  GtkWidget* note = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  AddClasses(note, {"card", "setup-card"});
  GtkWidget* icon = Icon("dialog-information-symbolic", tone);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
  gtk_widget_set_valign(icon, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(note), icon);
  GtkWidget* body = Label(text);
  gtk_widget_set_hexpand(body, TRUE);
  gtk_box_append(GTK_BOX(note), body);
  return note;
}

constexpr int kPictureHeight = 330;

// The pictures are 1220 × 2712 screenshots (13 MB each decoded). As on Windows,
// one is decoded only while its row is open, and here at the size it is shown
// (two pixels per point, for HiDPI): about 0.8 MB.
void ShowPicture(GtkPicture* picture, bool open) {
  if (!open) { gtk_picture_set_paintable(picture, nullptr); return; }
  const auto* file = static_cast<const char*>(g_object_get_data(G_OBJECT(picture), "file"));
  GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(file, -1, 2 * kPictureHeight, TRUE, nullptr);
  if (!pixbuf) return;
  GBytes* bytes = gdk_pixbuf_read_pixel_bytes(pixbuf);
  GdkTexture* texture = gdk_memory_texture_new(gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf),
                                               gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8, bytes,
                                               static_cast<gsize>(gdk_pixbuf_get_rowstride(pixbuf)));
  gtk_picture_set_paintable(picture, GDK_PAINTABLE(texture));
  g_object_unref(texture);
  g_bytes_unref(bytes);
  g_object_unref(pixbuf);
}

// A collapsed picture of the phone's settings, as on Windows.
GtkWidget* Picture(const std::string& title, const char* file) {
  GtkWidget* list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list");
  GtkWidget* expander = adw_expander_row_new();
  adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(expander), FALSE);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander), title.c_str());
  adw_expander_row_add_prefix(ADW_EXPANDER_ROW(expander), Icon("image-x-generic-symbolic"));
  GtkWidget* picture = gtk_picture_new();
  g_object_set_data_full(G_OBJECT(picture), "file", g_strdup((DataDir() / "images" / file).c_str()), g_free);
  gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
  gtk_widget_set_size_request(picture, -1, kPictureHeight);
  g_signal_connect(expander, "notify::expanded", G_CALLBACK(+[](GObject* row, GParamSpec*, gpointer picture) {
    ShowPicture(GTK_PICTURE(picture), adw_expander_row_get_expanded(ADW_EXPANDER_ROW(row)));
  }), picture);
  gtk_accessible_update_property(GTK_ACCESSIBLE(picture), GTK_ACCESSIBLE_PROPERTY_LABEL, title.c_str(), -1);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) edge(picture, 10);
  GtkWidget* row = gtk_list_box_row_new();
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_widget_set_focusable(row, FALSE);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), picture);
  adw_expander_row_add_row(ADW_EXPANDER_ROW(expander), row);
  gtk_list_box_append(GTK_LIST_BOX(list), expander);
  return list;
}

GtkWidget* IconLabel(const char* icon, const std::string& text) {
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
  if (icon) gtk_box_append(GTK_BOX(content), Icon(icon));
  gtk_box_append(GTK_BOX(content), gtk_label_new(text.c_str()));
  return content;
}

}  // namespace

GtkWidget* SukutWindow::BuildPhone() {
  GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  phone_scroll_ = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(phone_scroll_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(phone_scroll_, TRUE);
  GtkWidget* clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 760);
  phone_content_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(phone_content_, 18);
  adw_clamp_set_child(ADW_CLAMP(clamp), phone_content_);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(phone_scroll_), clamp);
  // The phone's card leads the page: who is connected, and in what state.
  GtkWidget* hero_clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(hero_clamp), 760);
  gtk_box_append(GTK_BOX(page), hero_clamp);
  gtk_box_append(GTK_BOX(page), phone_scroll_);

  // Step navigation, as on Windows: Back · Step n of 5 · Next.
  phone_footer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_bottom, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(phone_footer_, 10);
  phone_back_ = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(phone_back_), IconLabel(Ar() ? "go-next-symbolic" : "go-previous-symbolic", L("Back", "السابق")));
  Name(phone_back_, L("Back", "السابق"));
  gtk_widget_set_has_tooltip(phone_back_, FALSE);
  g_signal_connect(phone_back_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->phone_busy_) return;
    if (s->phone_task_ == PhoneTask::FullSetup && s->phone_step_ > 0) s->ShowPhoneTask(PhoneTask::FullSetup, s->phone_step_ - 1);
    else s->ShowPhoneTask(PhoneTask::Choices);
  }), this);
  phone_step_label_ = Label("", "dim-label", false);
  gtk_widget_add_css_class(phone_step_label_, "caption");
  gtk_label_set_xalign(GTK_LABEL(phone_step_label_), 0.5);
  gtk_widget_set_hexpand(phone_step_label_, TRUE);
  phone_next_ = gtk_button_new_with_label("");
  gtk_widget_add_css_class(phone_next_, "suggested-action");
  g_signal_connect(phone_next_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    if (s->phone_busy_) return;
    if (s->phone_step_ < kPhoneSteps - 1) s->ShowPhoneTask(PhoneTask::FullSetup, s->phone_step_ + 1);
    else s->ShowPhoneTask(PhoneTask::Choices);
  }), this);
  gtk_box_append(GTK_BOX(phone_footer_), phone_back_);
  gtk_box_append(GTK_BOX(phone_footer_), phone_step_label_);
  gtk_box_append(GTK_BOX(phone_footer_), phone_next_);
  gtk_box_append(GTK_BOX(page), phone_footer_);

  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  AddClasses(card, {"card", "phone-hero"});
  gtk_widget_set_vexpand(card, FALSE);
  // Same insets as the page content below, so the edges line up.
  for (auto edge : {gtk_widget_set_margin_top, gtk_widget_set_margin_start, gtk_widget_set_margin_end}) edge(card, 18);
  adw_clamp_set_child(ADW_CLAMP(hero_clamp), card);
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  // The image is the round badge itself, so the icon is drawn in its centre.
  phone_icon_ = Icon("phone-symbolic", "mode-off");
  gtk_widget_add_css_class(phone_icon_, "phone-badge");
  gtk_widget_set_valign(phone_icon_, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row), phone_icon_);
  GtkWidget* words = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_hexpand(words, TRUE);
  gtk_widget_set_valign(words, GTK_ALIGN_CENTER);
  phone_name_ = Label("", "phone-title");
  phone_connection_ = Label("", "dim-label");
  phone_version_ = Label("", "status-pill");
  gtk_widget_set_halign(phone_version_, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(words), phone_name_);
  gtk_box_append(GTK_BOX(words), phone_connection_);
  gtk_box_append(GTK_BOX(words), phone_version_);
  gtk_box_append(GTK_BOX(row), words);
  phone_refresh_ = IconButton("view-refresh-symbolic", L("Refresh connected phone", "تحديث الهاتف المتصل"));
  g_signal_connect(phone_refresh_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    static_cast<SukutWindow*>(self)->PhoneAction([](PhoneTools&, const PhoneDevice&) { return std::string("Phone connected."); });
  }), this);
  gtk_box_append(GTK_BOX(row), phone_refresh_);
  gtk_box_append(GTK_BOX(card), row);
  // Linux only: adb lists the phone as "no permissions". One administrator
  // prompt (polkit) fixes it; Sukut never asks for the password itself.
  phone_allow_ = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(phone_allow_), IconLabel("changes-allow-symbolic", L("Allow access", "السماح بالوصول")));
  AddClasses(phone_allow_, {"suggested-action", "pill"});
  Name(phone_allow_, L("Allow Sukut to access the phone", "السماح لسُكوت بالوصول إلى الهاتف"));
  gtk_widget_set_has_tooltip(phone_allow_, FALSE);
  gtk_widget_set_halign(phone_allow_, GTK_ALIGN_START);
  g_signal_connect(phone_allow_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    static_cast<SukutWindow*>(self)->PhoneAction([](PhoneTools& tools, const PhoneDevice&) { return tools.AllowUsbAccess(); }, false);
  }), this);
  gtk_box_append(GTK_BOX(card), phone_allow_);
  phone_picker_ = gtk_drop_down_new_from_strings(nullptr);
  Name(phone_picker_, L("Choose phone", "اختر الهاتف"));
  gtk_widget_set_has_tooltip(phone_picker_, FALSE);
  g_signal_connect(phone_picker_, "notify::selected", G_CALLBACK(+[](GObject* drop, GParamSpec*, gpointer self) {
    auto* s = static_cast<SukutWindow*>(self);
    const guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
    if (s->applying_ || index >= s->phone_devices_.size()) return;
    s->phone_serial_ = s->phone_devices_[index].serial;
    s->RefreshPhone();
  }), this);
  gtk_box_append(GTK_BOX(card), phone_picker_);
  RenderPhone();
  return page;
}

void SukutWindow::ShowPhoneTask(PhoneTask task, int step) {
  if (phone_busy_) return;
  phone_task_ = task;
  phone_step_ = std::clamp(step, 0, kPhoneSteps - 1);
  phone_message_.clear();
  phone_severity_ = Severity::None;
  RenderPhone();
}

void SukutWindow::SetPhoneState(std::vector<PhoneDevice> devices, std::optional<PhoneInfo> info, const std::string& message, Severity severity) {
  phone_devices_ = std::move(devices);
  const auto selected = PhoneTools::Select(phone_devices_, phone_serial_);
  phone_serial_ = selected ? selected->serial : "";
  phone_info_ = std::move(info);
  phone_info_serial_ = phone_serial_;
  phone_message_ = message;
  phone_severity_ = severity;
  RenderPhoneCard();
  RenderPhoneMessage();
}

void SukutWindow::PhoneChoices(GtkWidget* page) {
  // Cards, not a settings list: one featured route and the two tools beside each other.
  auto card = [this](const char* icon, const char* tone, const std::string& title, const std::string& detail, PhoneTask task, bool featured) {
    GtkWidget* button = gtk_button_new();
    AddClasses(button, {"card", featured ? "feature-card" : "tool-card"});
    Name(button, title);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, detail.c_str(), -1);
    gtk_widget_set_has_tooltip(button, FALSE);
    GtkWidget* box = gtk_box_new(featured ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, featured ? 16 : 10);
    GtkWidget* image = Icon(icon, tone);
    gtk_widget_add_css_class(image, "tool-icon");
    gtk_widget_set_halign(image, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), image);
    GtkWidget* words = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(words, TRUE);
    gtk_box_append(GTK_BOX(words), Label(title, "tool-title"));
    GtkWidget* sub = Label(detail, "dim-label");
    gtk_box_append(GTK_BOX(words), sub);
    gtk_box_append(GTK_BOX(box), words);
    if (featured) gtk_box_append(GTK_BOX(box), Icon(Ar() ? "go-previous-symbolic" : "go-next-symbolic"));
    gtk_button_set_child(GTK_BUTTON(button), box);
    g_object_set_data(G_OBJECT(button), "task", GINT_TO_POINTER(static_cast<int>(task)));
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer self) {
      static_cast<SukutWindow*>(self)->ShowPhoneTask(static_cast<PhoneTask>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "task"))));
    }), this);
    return button;
  };
  gtk_box_append(GTK_BOX(page), card("preferences-system-symbolic", "mode-2", L("Full setup", "الإعداد الكامل"),
                                     L("Connect, install and activate. We’ll guide you through each step.", "توصيل وتثبيت وتفعيل. نرشدك في كل خطوة."),
                                     PhoneTask::FullSetup, true));
  GtkWidget* tools_title = Label(L("Tools", "الأدوات"), "section-heading", false);
  gtk_widget_set_margin_top(tools_title, 6);
  gtk_box_append(GTK_BOX(page), tools_title);
  GtkWidget* tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_set_homogeneous(GTK_BOX(tools), TRUE);
  gtk_box_append(GTK_BOX(tools), card("system-run-symbolic", "ready-text", L("Activate Sukut", "تفعيل سُكوت"),
                                      L("Start the audio worker over USB.", "تشغيل عامل الصوت عبر USB."), PhoneTask::Activate, false));
  gtk_box_append(GTK_BOX(tools), card("system-software-install-symbolic", "accent-text", L("Install or update Sukut", "تثبيت سُكوت أو تحديثه"),
                                      L("Keeps your settings.", "مع الاحتفاظ بإعداداتك."), PhoneTask::Install, false));
  gtk_box_append(GTK_BOX(page), tools);
  GtkWidget* help = gtk_button_new_with_label(L("USB connection help", "المساعدة في اتصال USB"));
  AddClasses(help, {"flat", "accent-text"});
  gtk_widget_set_halign(help, GTK_ALIGN_START);
  g_signal_connect(help, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
    static_cast<SukutWindow*>(self)->ShowPhoneTask(PhoneTask::UsbHelp);
  }), this);
  gtk_box_append(GTK_BOX(page), help);
}

void SukutWindow::AddPhoneActions(GtkWidget* page, const char* en, const char* ar, const char* icon,
                                  std::function<std::string(PhoneTools&, const PhoneDevice&)> action) {
  phone_actions_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* primary = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(primary), IconLabel(icon, L(en, ar)));
  AddClasses(primary, {"suggested-action", "pill"});
  Name(primary, L(en, ar));
  gtk_widget_set_has_tooltip(primary, FALSE);
  auto* stored = new std::function<std::string(PhoneTools&, const PhoneDevice&)>(std::move(action));
  g_object_set_data_full(G_OBJECT(primary), "action", stored, [](gpointer d) {
    delete static_cast<std::function<std::string(PhoneTools&, const PhoneDevice&)>*>(d);
  });
  g_signal_connect(primary, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer self) {
    auto* run = static_cast<std::function<std::string(PhoneTools&, const PhoneDevice&)>*>(g_object_get_data(G_OBJECT(b), "action"));
    static_cast<SukutWindow*>(self)->PhoneAction(*run);
  }), this);
  gtk_box_append(GTK_BOX(phone_actions_), primary);
  gtk_box_append(GTK_BOX(page), phone_actions_);
  phone_progress_ = gtk_progress_bar_new();
  gtk_widget_set_visible(phone_progress_, FALSE);
  gtk_box_append(GTK_BOX(page), phone_progress_);
  phone_message_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  AddClasses(phone_message_box_, {"card", "setup-card"});
  phone_message_icon_ = Icon("dialog-information-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(phone_message_icon_), 20);
  gtk_widget_set_valign(phone_message_icon_, GTK_ALIGN_START);
  phone_message_label_ = Label("");
  gtk_label_set_selectable(GTK_LABEL(phone_message_label_), TRUE);
  gtk_widget_set_hexpand(phone_message_label_, TRUE);
  gtk_box_append(GTK_BOX(phone_message_box_), phone_message_icon_);
  gtk_box_append(GTK_BOX(phone_message_box_), phone_message_label_);
  gtk_box_append(GTK_BOX(page), phone_message_box_);
}

void SukutWindow::RenderPhone() {
  if (!phone_content_) return;
  const bool previous = applying_;
  applying_ = true;
  while (GtkWidget* child = gtk_widget_get_first_child(phone_content_)) gtk_box_remove(GTK_BOX(phone_content_), child);
  phone_actions_ = phone_progress_ = phone_message_box_ = phone_message_icon_ = phone_message_label_ = nullptr;
  GtkWidget* page = phone_content_;
  const auto developer = [&] {
    gtk_box_append(GTK_BOX(page), Picture(L("Show developer options", "عرض صورة خيارات المطوّر"), "phone-developer-options.png"));
  };
  const auto debugging = [&] {
    gtk_box_append(GTK_BOX(page), Picture(L("Show USB debugging", "عرض صورة تصحيح USB"), "phone-usb-debugging.png"));
  };
  const auto activate_page = [&] {
    gtk_box_append(GTK_BOX(page), Heading(L("Activate Sukut", "تفعيل سُكوت")));
    gtk_box_append(GTK_BOX(page), Label(L("Keep your phone unlocked. Activate Sukut, then choose a mode and press Start on the phone.",
                                          "أبقِ الهاتف مفتوحًا. فعّل سُكوت ثم اختر وضعًا واضغط زر التشغيل في الهاتف.")));
    AddPhoneActions(page, "Activate Sukut", "تفعيل سُكوت", "media-playback-start-symbolic",
                    [](PhoneTools& tools, const PhoneDevice& device) { return tools.Activate(device); });
    gtk_box_append(GTK_BOX(page), Note("accent-text", L("Activation does not start audio. Press Start in Sukut on the phone. Keep debugging enabled while using Sukut if turning it off stops the audio worker.",
                                                        "التفعيل لا يشغّل الصوت. اضغط زر التشغيل في سُكوت على الهاتف. أبقِ تصحيح الأخطاء مفعّلًا أثناء استخدام سُكوت إذا كان إيقافه يوقف عامل الصوت.")));
    GtkWidget* stop = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(stop), IconLabel("media-playback-stop-symbolic", L("Deactivate worker", "إلغاء تفعيل عامل الصوت")));
    gtk_widget_add_css_class(stop, "pill");
    g_signal_connect(stop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
      static_cast<SukutWindow*>(self)->PhoneAction([](PhoneTools& tools, const PhoneDevice& device) { return tools.Deactivate(device); });
    }), this);
    gtk_box_append(GTK_BOX(phone_actions_), stop);
  };
  const auto install_page = [&] {
    gtk_box_append(GTK_BOX(page), Heading(L("Install or update Sukut", "تثبيت سُكوت أو تحديثه")));
    gtk_box_append(GTK_BOX(page), Label(L("Your settings and newer versions are kept.", "نحتفظ بإعداداتك والنسخ الأحدث.")));
    AddPhoneActions(page, "Set up Sukut", "إعداد سُكوت", "system-software-install-symbolic",
                    [](PhoneTools& tools, const PhoneDevice& device) { return tools.Setup(device); });
  };

  if (phone_task_ == PhoneTask::Choices) {
    PhoneChoices(page);
  } else if (phone_task_ == PhoneTask::UsbHelp) {
    gtk_box_append(GTK_BOX(page), Heading(L("Connect your phone", "توصيل هاتفك")));
    gtk_box_append(GTK_BOX(page), Bullets({{"preferences-system-symbolic", "mode-2", L("1 · Enable developer options", "١ · تفعيل خيارات المطوّر"),
        L("Settings → About phone → tap Build number seven times. On Xiaomi, tap OS version.",
          "الإعدادات ← حول الهاتف ← اضغط رقم الإصدار سبع مرات. في شاومي اضغط إصدار النظام.")}}));
    developer();
    gtk_box_append(GTK_BOX(page), Bullets({{"changes-prevent-symbolic", "mode-2", L("2 · Enable USB debugging", "٢ · تفعيل تصحيح USB"),
        L("Open Developer options and turn on USB debugging.", "افتح خيارات المطوّر وشغّل تصحيح USB.")}}));
    debugging();
    gtk_box_append(GTK_BOX(page), Bullets({{"object-select-symbolic", "mode-1", L("3 · Connect and approve", "٣ · التوصيل والموافقة"),
        L("Use a USB data cable, unlock the phone and approve this computer on the phone.",
          "استخدم كابل USB للبيانات، وافتح قفل الهاتف ووافق على اتصال هذا الحاسوب.")}}));
    // Linux only: a phone the computer won't let Sukut open gets Allow access on the card.
    gtk_box_append(GTK_BOX(page), Note("mode-2", L("If the card below says Sukut can’t access the phone, choose Allow access. Your computer asks for an administrator password once.",
                                                  "إذا ظهر في البطاقة أدناه أن سُكوت لا يستطيع الوصول إلى الهاتف، فاختر «السماح بالوصول». يطلب حاسوبك كلمة مرور المسؤول مرة واحدة.")));
    AddPhoneActions(page, "Check connection", "التحقّق من الاتصال", "emblem-ok-symbolic",
                    [](PhoneTools&, const PhoneDevice&) { return std::string("Phone connected. Install Sukut or activate its audio worker."); });
  } else if (phone_task_ == PhoneTask::Activate) {
    activate_page();
  } else if (phone_task_ == PhoneTask::Install) {
    install_page();
  } else {
    switch (phone_step_) {
      case 0:
        gtk_box_append(GTK_BOX(page), Heading(L("Enable developer options", "تفعيل خيارات المطوّر")));
        gtk_box_append(GTK_BOX(page), Bullets({
            {"preferences-system-symbolic", "accent-text", L("Open Settings → About phone", "افتح الإعدادات ← حول الهاتف"),
             L("Find Build number. On Xiaomi, use OS version.", "ابحث عن رقم الإصدار. في شاومي استخدم إصدار النظام.")},
            {"object-select-symbolic", "ready-text", L("Tap seven times", "اضغط سبع مرات"),
             L("Enter your phone PIN if asked. Then open Developer options.", "أدخل رمز قفل هاتفك إذا طُلب، ثم افتح خيارات المطوّر.")}}));
        developer();
        break;
      case 1:
        gtk_box_append(GTK_BOX(page), Heading(L("Allow USB debugging", "السماح بتصحيح USB")));
        gtk_box_append(GTK_BOX(page), Bullets({
            {"preferences-system-symbolic", "accent-text", L("Turn on USB debugging", "شغّل تصحيح USB"),
             L("You’ll find it in Developer options.", "تجده في خيارات المطوّر.")},
            {"computer-symbolic", "ready-text", L("Connect and allow this computer", "وصّل الهاتف واسمح لهذا الحاسوب"),
             L("Use a USB data cable. Keep the phone unlocked and tap Allow.", "استخدم كابل USB للبيانات، وأبقِ الهاتف مفتوحًا ثم اضغط السماح.")}}));
        debugging();
        gtk_box_append(GTK_BOX(page), Note("mode-2", L("If installation is blocked, also allow Install via USB on the phone.",
                                                      "إذا حُظر التثبيت، اسمح أيضًا بـ«التثبيت عبر USB» في الهاتف.")));
        break;
      case 2: install_page(); break;
      case 3: activate_page(); break;
      case 4:
        gtk_box_append(GTK_BOX(page), Heading(L("Ready on your phone", "جاهز على هاتفك")));
        gtk_box_append(GTK_BOX(page), Bullets({
          {"media-playback-start-symbolic", "ready-text", L("Choose a mode and press Start", "اختر وضعًا واضغط التشغيل"),
           L("Open Home in Sukut on the phone. You can unplug USB.", "افتح الرئيسية في سُكوت على الهاتف. يمكنك فصل USB.")},
          {"io.github.altarafiya.Sukut-lightning-symbolic", "accent-text", L("If activation is lost", "إذا فُقد التفعيل"),
           L("Connect USB and choose Phone → Activate Sukut here. The phone offers a separate guide when you ask for help.", "وصّل USB واختر هنا الهاتف ← تفعيل سُكوت. يعرض الهاتف دليلًا منفصلًا عندما تطلب المساعدة.")}}));
        break;
    }
  }

  const bool setup = phone_task_ == PhoneTask::FullSetup;
  gtk_widget_set_visible(phone_footer_, phone_task_ != PhoneTask::Choices);
  gtk_widget_set_visible(phone_next_, setup);
  const std::string step = !setup ? ""
                           : Ar() ? "الخطوة " + std::to_string(phone_step_ + 1) + " من " + std::to_string(kPhoneSteps)
                                  : "Step " + std::to_string(phone_step_ + 1) + " of " + std::to_string(kPhoneSteps);
  gtk_label_set_text(GTK_LABEL(phone_step_label_), step.c_str());
  gtk_button_set_label(GTK_BUTTON(phone_next_), phone_step_ == kPhoneSteps - 1 ? L("Done", "تمّ") : L("Next", "التالي"));
  gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(phone_scroll_)), 0);
  applying_ = previous;
  RenderPhoneCard();
  RenderPhoneMessage();
}

void SukutWindow::RenderPhoneCard() {
  if (!phone_name_) return;
  const bool previous = applying_;
  applying_ = true;
  const auto device = std::find_if(phone_devices_.begin(), phone_devices_.end(), [&](const auto& d) { return d.serial == phone_serial_; });
  const bool found = device != phone_devices_.end();
  const bool connected = found && device->state == "device";
  const PhoneInfo* info = phone_info_ && phone_info_serial_ == phone_serial_ ? &*phone_info_ : nullptr;
  gtk_label_set_text(GTK_LABEL(phone_name_), info ? info->name.c_str() : found ? device->model.c_str() : L("No phone connected", "لا يوجد هاتف متصل"));
  std::string connection;
  if (!phone_card_note_.empty()) connection = PhoneText(phone_card_note_);
  else if (connected) {
    // Wireless debugging serials are host:port.
    // Latin parts are isolated in Arabic, so the Arabic order of the parts holds.
    connection = device->serial.find(':') != std::string::npos ? L("Connected over the network", "متصل عبر الشبكة")
                 : Ar() ? "متصل عبر " + Isolate("USB") : "Connected via USB";
    if (info && !info->android.empty()) connection += " · " + (Ar() ? Isolate("Android " + info->android) : "Android " + info->android);
  } else if (found && device->state == "unauthorized") connection = L("Approve USB debugging on your phone", "وافق على تصحيح USB في هاتفك");
  else if (found && device->state == "no permissions") connection = L("Sukut can’t access the phone yet", "لا يستطيع سُكوت الوصول إلى الهاتف بعد");
  else if (phone_devices_.size() > 1) connection = L("Choose your phone below", "اختر هاتفك أدناه");
  else connection = L("Connect your phone by USB", "وصّل هاتفك عبر USB");
  gtk_label_set_text(GTK_LABEL(phone_connection_), connection.c_str());
  gtk_widget_set_visible(phone_allow_, !InSandbox() && !connected && std::any_of(phone_devices_.begin(), phone_devices_.end(),
                                                                 [](const auto& d) { return d.state == "no permissions"; }));
  SetClass(phone_icon_, "ready-text", connected);
  SetClass(phone_icon_, "mode-off", !connected);
  gtk_widget_set_visible(phone_version_, connected && info);
  if (info) {
    const std::string app = "Sukut " + (info->sukut_version.empty() || !info->sukut_code ? (info->sukut_code ? std::to_string(*info->sukut_code) : "")
                                                                                         : info->sukut_version);
    const std::string version = !info->sukut_code ? std::string(L("Sukut not installed", "سُكوت غير مثبّت"))
                                : (Ar() ? Isolate(app) : app) + " · " +
                                      (info->worker_ready ? L("Worker ready", "عامل الصوت جاهز") : L("Activation needed", "يلزم التفعيل"));
    gtk_label_set_text(GTK_LABEL(phone_version_), version.c_str());
    SetClass(phone_version_, "ready-text", info->sukut_code && info->worker_ready);
    SetClass(phone_version_, "warning-text", !info->sukut_code || !info->worker_ready);
  }
  // The picker appears only when more than one phone is connected.
  std::vector<std::string> labels;
  for (const auto& d : phone_devices_) labels.push_back(d.Label());
  std::vector<const char*> strings;
  for (const auto& l : labels) strings.push_back(l.c_str());
  strings.push_back(nullptr);
  gtk_drop_down_set_model(GTK_DROP_DOWN(phone_picker_), G_LIST_MODEL(gtk_string_list_new(strings.data())));
  if (found) gtk_drop_down_set_selected(GTK_DROP_DOWN(phone_picker_), static_cast<guint>(device - phone_devices_.begin()));
  gtk_widget_set_visible(phone_picker_, phone_devices_.size() > 1);
  for (GtkWidget* w : {phone_refresh_, phone_allow_, phone_picker_, phone_back_, phone_next_}) gtk_widget_set_sensitive(w, !phone_busy_);
  if (phone_actions_) gtk_widget_set_sensitive(phone_actions_, !phone_busy_);
  applying_ = previous;
}

void SukutWindow::RenderPhoneMessage() {
  if (phone_progress_) gtk_widget_set_visible(phone_progress_, phone_busy_);
  if (!phone_message_box_) return;
  gtk_widget_set_visible(phone_message_box_, !phone_message_.empty());
  gtk_label_set_text(GTK_LABEL(phone_message_label_), PhoneText(phone_message_).c_str());
  const char* tone = phone_severity_ == Severity::Success ? "ready-text" : phone_severity_ == Severity::Warning ? "warning-text" : "accent-text";
  for (const char* c : {"ready-text", "warning-text", "accent-text"}) gtk_widget_remove_css_class(phone_message_icon_, c);
  gtk_widget_add_css_class(phone_message_icon_, tone);
  gtk_image_set_from_icon_name(GTK_IMAGE(phone_message_icon_), phone_severity_ == Severity::Success ? "object-select-symbolic"
                                                               : phone_severity_ == Severity::Warning ? "dialog-warning-symbolic"
                                                                                                     : "dialog-information-symbolic");
}

void SukutWindow::PhoneAction(std::function<std::string(PhoneTools&, const PhoneDevice&)> action, bool needs_phone) {
  if (phone_busy_ || !phone_tools_) return;
  phone_busy_ = true;
  phone_message_ = "Working… Check the phone for prompts.";
  phone_severity_ = Severity::Info;
  RenderPhoneCard();
  RenderPhoneMessage();
  phone_pulse_ = g_timeout_add(120, [](gpointer self) -> gboolean {
    auto* s = static_cast<SukutWindow*>(self);
    if (!s->phone_busy_) { s->phone_pulse_ = 0; return G_SOURCE_REMOVE; }
    if (s->phone_progress_) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(s->phone_progress_));
    return G_SOURCE_CONTINUE;
  }, this);
  struct Outcome {
    bool ok = false;
    std::string text, serial;
    std::vector<PhoneDevice> devices;
  };
  std::thread([this, tools = phone_tools_, previous = phone_serial_, needs_phone, action = std::move(action)] {
    Outcome outcome;
    try {
      if (needs_phone) {
        outcome.devices = tools->Devices();
        const auto device = PhoneTools::Select(outcome.devices, previous);
        if (outcome.devices.empty()) throw PhoneError("Connect your phone by USB and allow debugging.");
        if (!device) throw PhoneError("Select your phone first.");
        outcome.serial = device->serial;
        if (device->state != "device") throw PhoneError(PhoneTools::NotReady(*device));
        outcome.text = action(*tools, *device);
      } else {
        outcome.text = action(*tools, PhoneDevice{});
        outcome.devices = tools->Devices();
      }
      outcome.ok = true;
    } catch (const std::exception& error) {
      outcome.text = error.what();
    }
    Post([this, outcome] {
      phone_busy_ = false;
      phone_card_note_.clear();
      if (!outcome.devices.empty() || outcome.ok) phone_devices_ = outcome.devices;
      if (!outcome.serial.empty()) phone_serial_ = outcome.serial;
      phone_message_ = outcome.text;
      phone_severity_ = outcome.ok && outcome.text.find("Developer options shortcut setup was not allowed.") == std::string::npos
          ? Severity::Success : Severity::Warning;
      RenderPhoneCard();
      RenderPhoneMessage();
      RefreshPhone();
    });
  }).detach();
}

void SukutWindow::RefreshPhone() {
  if (!phone_tools_ || phone_busy_ || phone_refreshing_) return;
  phone_refreshing_ = true;
  std::thread([this, tools = phone_tools_, previous = phone_serial_] {
    std::vector<PhoneDevice> devices;
    std::optional<PhoneInfo> info;
    std::string serial, note;
    try {
      devices = tools->Devices();
      if (const auto device = PhoneTools::Select(devices, previous)) {
        serial = device->serial;
        if (device->state == "device") {
          try { info = tools->Inspect(*device); } catch (const std::exception&) {}
        }
      }
    } catch (const std::exception& error) {
      note = error.what();  // adb is missing or failed: say so on the card
    }
    Post([this, devices, info, serial, note] {
      phone_refreshing_ = false;
      phone_devices_ = devices;
      phone_serial_ = serial;
      phone_info_ = info;
      phone_info_serial_ = serial;
      phone_card_note_ = note;
      RenderPhoneCard();
    });
  }).detach();
}

std::string SukutWindow::PhoneText(const std::string& english) const {
  // The error rewrites apply in both languages, as on Windows (PhoneAction).
  struct Pair { const char* match; const char* en; const char* ar; };
  static const Pair rewrites[] = {
      {"timeout", "Timed out. Check the phone and connection, then retry.", "انتهت المهلة. تحقّق من الهاتف والاتصال، ثم أعد المحاولة."},
      {"INSTALL_FAILED_USER_RESTRICTED", "Approve the installation on your phone. On Xiaomi, enable Install via USB in Developer options, then retry.",
       "وافق على التثبيت في هاتفك. في شاومي، فعّل «التثبيت عبر USB» من خيارات المطوّر، ثم أعد المحاولة."},
      {"INSTALL_FAILED_UPDATE_INCOMPATIBLE", "The installed app has a different signing key. Use an update signed with the same key; your installed app and data have been kept.",
       "للنسخة المثبّتة مفتاح توقيع مختلف. استخدم تحديثًا بالمفتاح نفسه. احتُفظ بالتطبيق المثبّت وبياناته."},
      {"Sukut APK", "The included Sukut APK is missing or damaged. Reinstall Sukut for Linux.", "ملف تثبيت سُكوت المرفق مفقود أو تالف. أعد تثبيت سُكوت للينكس."},
      {"Sukut is not installed", "Sukut is not installed. Choose Install Sukut first.", "سُكوت غير مثبّت. اختر تثبيت سُكوت أولًا."},
      {"Could not start ADB", "adb could not start. Install the adb package.", "تعذّر تشغيل adb. ثبّت حزمة adb."},
      {"Sukut activation script", "The included activation script is missing or damaged. Reinstall Sukut for Linux.",
       "سكربت التفعيل المرفق مفقود أو تالف. أعد تثبيت سُكوت للينكس."},
  };
  for (const auto& rewrite : rewrites)
    if (english.find(rewrite.match) != std::string::npos) return L(rewrite.en, rewrite.ar);
  if (!Ar()) return english;
  static const std::pair<const char*, const char*> arabic[] = {
      {"Working… Check the phone for prompts.", "جارٍ التنفيذ… تحقّق من طلبات الهاتف."},
      {"Phone connected.", "الهاتف متصل."},
      {"Phone connected. Install Sukut or activate its audio worker.", "الهاتف متصل. ثبّت سُكوت أو فعّل عامل الصوت."},
      {"Sukut is already installed. Your settings are kept.", "سُكوت مثبّت بالفعل. إعداداتك محفوظة."},
      {"Sukut installed. Continue to open it on the phone.", "تم تثبيت سُكوت. تابع لفتحه في الهاتف."},
      {"Sukut updated. Your settings are kept.", "تم تحديث سُكوت. إعداداتك محفوظة."},
      {"Sukut is already up to date. Your settings are kept.", "سُكوت محدّث بالفعل. إعداداتك محفوظة."},
      {"Sukut opened. Activate its audio worker, then press Start on the phone.", "تم فتح سُكوت. فعّل عامل الصوت، ثم اضغط زر التشغيل في الهاتف."},
      {"Sukut is installed and ready for activation.", "سُكوت مثبّت وجاهز للتفعيل."},
      {"Sukut audio worker is already ready. Press Start on the phone.", "عامل الصوت جاهز بالفعل. اضغط زر التشغيل في الهاتف."},
      {"Sukut audio worker is already ready. Choose a mode and press Start on the phone. You can unplug USB.",
       "عامل الصوت جاهز بالفعل. اختر وضعًا واضغط التشغيل في الهاتف. يمكنك فصل USB."},
      {"Sukut audio worker is ready. Choose a mode and press Start on the phone. You can unplug USB.",
       "عامل الصوت جاهز. اختر وضعًا واضغط التشغيل في الهاتف. يمكنك فصل USB."},
      {"Sukut audio worker is ready. Developer options shortcut setup was not allowed. The worker can be used without the shortcut.",
       "عامل الصوت جاهز. لم يُسمح بإعداد اختصار خيارات المطوّر. يمكنك استخدام عامل الصوت دون الاختصار."},
      {"Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart.",
       "عامل الصوت جاهز. يمكنك فصل USB والضغط على زر التشغيل في الهاتف. أعد التفعيل بعد إعادة تشغيل الهاتف."},
      {"Sukut audio worker deactivated. Activate it again before using Start.", "أُلغي تفعيل عامل الصوت. فعّله مجدّدًا قبل استخدام زر التشغيل."},
      {"Sukut worker is no longer ready. Keep the phone unlocked and activate again.", "لم يعد عامل سُكوت جاهزًا. أبقِ الهاتف مفتوحًا ثم أعد التفعيل."},
      {"Unlock the phone, allow USB debugging, then refresh devices.", "افتح قفل الهاتف واسمح بتصحيح USB، ثم حدّث قائمة الأجهزة."},
      {"Connect your phone by USB and allow debugging.", "وصّل الهاتف عبر USB واسمح بتصحيح USB."},
      {"Select your phone first.", "اختر هاتفك أولًا."},
      {"Allow USB debugging on the phone, then try again.", "وافق على تصحيح USB في الهاتف، ثم أعد المحاولة."},
      {"The phone is offline. Reconnect the USB cable.", "الهاتف غير متصل. أعد توصيل كابل USB."},
      {"Update Sukut on your phone before activation.", "حدّث سُكوت على هاتفك قبل التفعيل."},
      {"USB access is closed to Sukut on this computer. On Android 11 or newer, open Sukut on the phone and activate it with Wireless debugging.", "الوصول عبر USB مغلق أمام سُكوت على هذا الحاسوب. في أندرويد 11 أو أحدث، افتح سُكوت على الهاتف وفعّله عبر تصحيح الأخطاء اللاسلكي."},
      {"Sukut can't access the phone yet. Choose Allow access.", "لا يستطيع سُكوت الوصول إلى الهاتف بعد. اختر «السماح بالوصول»."},
      {"USB access allowed. Unplug the phone and plug it in again.", "سُمح بالوصول عبر USB. افصل الهاتف ثم وصّله من جديد."},
      {"USB access allowed. Unplug the phone and plug it in again. If it still isn't listed, sign out and sign in again.",
       "سُمح بالوصول عبر USB. افصل الهاتف ثم وصّله من جديد. إن لم يظهر بعد، فسجّل الخروج ثم سجّل الدخول من جديد."},
      {"USB access was not allowed. Choose Allow access to try again.", "لم يُسمح بالوصول عبر USB. اختر «السماح بالوصول» لإعادة المحاولة."},
      {"USB access can't be fixed from this build. Install Sukut for Linux.", "لا يمكن إصلاح الوصول عبر USB من هذه النسخة. ثبّت سُكوت للينكس."},
      {"Could not ask for administrator permission. Reinstall Sukut for Linux.", "تعذّر طلب إذن المسؤول. أعد تثبيت سُكوت للينكس."},
  };
  for (const auto& [en, ar] : arabic)
    if (english == en) return ar;
  if (english.rfind("Could not allow USB access.", 0) == 0)
    return "تعذّر السماح بالوصول عبر USB." + english.substr(std::string("Could not allow USB access.").size());
  if (english.rfind("Sukut worker did not connect.", 0) == 0)
    return "لم يتصل عامل سُكوت. أبقِ الهاتف مفتوحًا وافتح سُكوت، ثم أعد المحاولة." + english.substr(std::string("Sukut worker did not connect. Keep the phone unlocked and open Sukut, then retry.").size());
  return english;
}

}  // namespace sukut::window
