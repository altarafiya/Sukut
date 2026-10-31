#include "window/Widgets.h"

#include <cmath>

#include "window/Style.h"

namespace sukut::window {
namespace {

void Rgb(cairo_t* cr, const double (&c)[3]) { cairo_set_source_rgb(cr, c[0], c[1], c[2]); }

// Windows BrandAnimation.cs: a bright hue wheel kept above 55/255 per channel.
void BrandHue(double hue, double out[3]) {
  hue -= std::floor(hue);
  const double k = hue * 6, x = 1 - std::fabs(std::fmod(k, 2.0) - 1);
  double r, g, b;
  if (k < 1) { r = 1; g = x; b = 0; } else if (k < 2) { r = x; g = 1; b = 0; } else if (k < 3) { r = 0; g = 1; b = x; }
  else if (k < 4) { r = 0; g = x; b = 1; } else if (k < 5) { r = x; g = 0; b = 1; } else { r = 1; g = 0; b = x; }
  out[0] = (55 + r * 200) / 255.0; out[1] = (55 + g * 200) / 255.0; out[2] = (55 + b * 200) / 255.0;
}

void RedrawOnTheme(GtkWidget* widget) {
  AdwStyleManager* manager = adw_style_manager_get_default();
  for (const char* signal : {"notify::dark", "notify::high-contrast"})
    g_signal_connect_object(manager, signal, G_CALLBACK(+[](GObject*, GParamSpec*, gpointer w) { gtk_widget_queue_draw(GTK_WIDGET(w)); }),
                            widget, G_CONNECT_DEFAULT);
}

struct Credit {
  guint tick = 0;
  double phase = 0;
};

constexpr const char* kCreditText = "الطرفية";

PangoLayout* CreditLayout(PangoContext* context) {
  PangoLayout* layout = pango_layout_new(context);
  pango_layout_set_text(layout, kCreditText, -1);
  PangoFontDescription* font = pango_font_description_from_string("Handjet Bold");
  pango_font_description_set_absolute_size(font, 36 * PANGO_SCALE);
  pango_layout_set_font_description(layout, font);
  pango_font_description_free(font);
  return layout;
}

bool InView(GtkWidget* widget) {
  GtkWidget* viewport = gtk_widget_get_ancestor(widget, GTK_TYPE_SCROLLED_WINDOW);
  if (viewport == nullptr) return gtk_widget_get_mapped(widget);
  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(widget, viewport, &bounds)) return false;
  const double height = gtk_widget_get_height(viewport);
  return bounds.origin.y + bounds.size.height > 0 && bounds.origin.y < height;
}

bool Animations() {
  gboolean enabled = TRUE;
  g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &enabled, nullptr);
  return enabled && !CurrentPalette().high_contrast;
}

}  // namespace

void Name(GtkWidget* widget, const std::string& name) {
  gtk_widget_set_tooltip_text(widget, name.c_str());
  gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, name.c_str(), -1);
  g_object_set_data_full(G_OBJECT(widget), "sukut-name", g_strdup(name.c_str()), g_free);
}

const char* NameOf(GtkWidget* widget) { return static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "sukut-name")); }

GtkWidget* Label(const std::string& text, const char* css_class, bool wrap) {
  GtkWidget* label = gtk_label_new(text.c_str());
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  if (wrap) {
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  }
  if (css_class) gtk_widget_add_css_class(label, css_class);
  return label;
}

GtkWidget* Icon(const char* icon_name, const char* css_class) {
  GtkWidget* image = gtk_image_new_from_icon_name(icon_name);
  if (css_class) gtk_widget_add_css_class(image, css_class);
  return image;
}

GtkWidget* IconButton(const char* icon_name, const std::string& name) {
  GtkWidget* button = gtk_button_new_from_icon_name(icon_name);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  Name(button, name);
  return button;
}

void AddClasses(GtkWidget* widget, std::initializer_list<const char*> classes) {
  for (const char* css_class : classes) gtk_widget_add_css_class(widget, css_class);
}

void SetClass(GtkWidget* widget, const char* css_class, bool on) {
  if (on) gtk_widget_add_css_class(widget, css_class);
  else gtk_widget_remove_css_class(widget, css_class);
}

GtkWidget* SleepFace(int size) {
  GtkWidget* area = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), size);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), size);
  gtk_widget_set_direction(area, GTK_TEXT_DIR_LTR);
  gtk_drawing_area_set_draw_func(
      GTK_DRAWING_AREA(area),
      [](GtkDrawingArea* self, cairo_t* cr, int width, int, gpointer) {
        const Palette p = CurrentPalette();
        GdkRGBA fg;
        gtk_widget_get_color(GTK_WIDGET(self), &fg);
        const double scale = width / 48.0;
        cairo_scale(cr, scale, scale);
        const auto ink = [&] { if (p.high_contrast) gdk_cairo_set_source_rgba(cr, &fg); else Rgb(cr, p.accent); };
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_arc(cr, 21, 29, 17, 0, 2 * G_PI);
        if (!p.high_contrast) { Rgb(cr, p.accent_container); cairo_fill_preserve(cr); }
        ink();
        cairo_set_line_width(cr, 1.8);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 2.4);
        cairo_move_to(cr, 11, 27); cairo_curve_to(cr, 13, 30, 17, 30, 19, 27);
        cairo_move_to(cr, 24, 27); cairo_curve_to(cr, 26, 30, 30, 30, 32, 27);
        cairo_move_to(cr, 33, 3.5); cairo_line_to(cr, 42, 3.5); cairo_line_to(cr, 33, 12.5); cairo_line_to(cr, 42, 12.5);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, 40, 17); cairo_line_to(cr, 46, 17); cairo_line_to(cr, 40, 23); cairo_line_to(cr, 46, 23);
        cairo_stroke(cr);
        cairo_save(cr);
        cairo_translate(cr, 21.5, 38);
        cairo_scale(cr, 2.5, 3);
        cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
        cairo_restore(cr);
        cairo_fill(cr);
      },
      nullptr, nullptr);
  RedrawOnTheme(area);
  return area;
}

GtkWidget* BrandCredit() {
  GtkWidget* area = gtk_drawing_area_new();
  auto* state = new Credit{};
  g_object_set_data_full(G_OBJECT(area), "credit", state, [](gpointer d) { delete static_cast<Credit*>(d); });
  gtk_widget_set_direction(area, GTK_TEXT_DIR_LTR);
  gtk_widget_set_halign(area, GTK_ALIGN_CENTER);
  Name(area, "الطرفية");
  gtk_accessible_update_property(GTK_ACCESSIBLE(area), GTK_ACCESSIBLE_PROPERTY_LABEL, kCreditText, -1);
  gtk_widget_set_has_tooltip(area, FALSE);
  {
    PangoLayout* layout = CreditLayout(gtk_widget_get_pango_context(area));
    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), logical.width + 8);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), logical.height + 4);
    g_object_unref(layout);
  }
  gtk_drawing_area_set_draw_func(
      GTK_DRAWING_AREA(area),
      [](GtkDrawingArea* self, cairo_t* cr, int width, int height, gpointer data) {
        auto* credit = static_cast<Credit*>(data);
        PangoLayout* layout = CreditLayout(gtk_widget_get_pango_context(GTK_WIDGET(self)));
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, nullptr, &logical);
        cairo_move_to(cr, (width - logical.width) / 2.0, (height - logical.height) / 2.0);
        pango_cairo_layout_path(cr, layout);
        g_object_unref(layout);
        if (CurrentPalette().high_contrast) {
          GdkRGBA fg;
          gtk_widget_get_color(GTK_WIDGET(self), &fg);
          gdk_cairo_set_source_rgba(cr, &fg);
        } else {
          cairo_pattern_t* gradient = cairo_pattern_create_linear(0, 0, width, 0);
          const double offsets[3] = {0, 0.5, 1}, phases[3] = {0, 0.33, 0.66};
          for (int i = 0; i < 3; ++i) {
            double c[3];
            BrandHue(phases[i] + credit->phase, c);
            cairo_pattern_add_color_stop_rgb(gradient, offsets[i], c[0], c[1], c[2]);
          }
          cairo_set_source(cr, gradient);
          cairo_pattern_destroy(gradient);
        }
        cairo_fill(cr);
      },
      state, nullptr);
  // Animate only while mapped, in view and allowed; the frame clock stops when the window is hidden.
  g_signal_connect(area, "map", G_CALLBACK(+[](GtkWidget* w, gpointer) {
    auto* credit = static_cast<Credit*>(g_object_get_data(G_OBJECT(w), "credit"));
    if (credit->tick) return;
    credit->tick = gtk_widget_add_tick_callback(
        w,
        [](GtkWidget* widget, GdkFrameClock* clock, gpointer) -> gboolean {
          auto* c = static_cast<Credit*>(g_object_get_data(G_OBJECT(widget), "credit"));
          if (!Animations() || !InView(widget)) return G_SOURCE_CONTINUE;
          c->phase = std::fmod(gdk_frame_clock_get_frame_time(clock) / 8.9e6, 1.0);
          gtk_widget_queue_draw(widget);
          return G_SOURCE_CONTINUE;
        },
        nullptr, nullptr);
  }), nullptr);
  g_signal_connect(area, "unmap", G_CALLBACK(+[](GtkWidget* w, gpointer) {
    auto* credit = static_cast<Credit*>(g_object_get_data(G_OBJECT(w), "credit"));
    if (credit->tick) gtk_widget_remove_tick_callback(w, credit->tick);
    credit->tick = 0;
  }), nullptr);
  RedrawOnTheme(area);
  return area;
}

GtkWidget* HelpCard(const char* icon_name, const char* tone, const std::string& title, std::initializer_list<std::string> points) {
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  AddClasses(card, {"card", "help-card"});
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget* icon = Icon(icon_name, tone);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
  if (std::string(icon_name).find("music-filter") != std::string::npos) gtk_widget_set_direction(icon, GTK_TEXT_DIR_LTR);
  gtk_box_append(GTK_BOX(header), icon);
  GtkWidget* heading = Label(title, "help-title");
  gtk_widget_add_css_class(heading, tone);
  gtk_box_append(GTK_BOX(header), heading);
  gtk_box_append(GTK_BOX(card), header);
  for (const auto& point : points) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* bullet = gtk_label_new("•");
    gtk_widget_set_valign(bullet, GTK_ALIGN_START);
    AddClasses(bullet, {tone, "heading"});
    gtk_box_append(GTK_BOX(row), bullet);
    GtkWidget* text = Label(point, "dim-label");
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(card), row);
  }
  return card;
}

}  // namespace sukut::window
