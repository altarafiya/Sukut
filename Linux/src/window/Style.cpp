#include "window/Style.h"

#include "audio/Sandbox.h"

#include <cstdlib>
#include <string>
#include <system_error>

#include <fontconfig/fontconfig.h>

namespace sukut::window {
namespace {

// Layout and type; the same in every theme. English uses the desktop's own
// font (GNOME's Adwaita Sans by default); Arabic uses IBM Plex Sans Arabic, a
// size larger because Arabic letters run smaller than Latin at the same size.
// Text sizes are in em so both follow the user's font size.
constexpr const char* kBase = R"css(
window.sukut.arabic { font-family: "IBM Plex Sans Arabic", sans-serif; font-size: 1.15em; }
.status-card { padding: 16px; }
.status-title { font-size: 1.35em; font-weight: 600; }
.status-glyph { -gtk-icon-size: 28px; }
.power-button { min-width: 92px; min-height: 30px; padding: 4px 14px; border-radius: 6px; font-weight: 600; }
.mode-icon { -gtk-icon-size: 20px; }
.playback-card { padding: 16px; }
.section-heading { font-weight: 600; }
.buffer-choice { padding: 2px 4px; }
.buffer-choice label { font-size: 0.85em; font-weight: 600; }
.help-card { padding: 12px 14px; border-radius: 10px; }
.help-title { font-size: 1.1em; font-weight: 600; }
.help-section { font-size: 1.25em; font-weight: 600; }
.setup-title { font-size: 1.65em; font-weight: 600; }
.setup-card { padding: 14px; }
.prayer { font-family: "IBM Plex Sans Arabic"; font-size: 1.25em; }
.about-name { font-family: "IBM Plex Sans Arabic"; font-size: 1.5em; font-weight: 600; }
.about-card { padding: 18px 16px; }
.dot { min-width: 8px; min-height: 8px; border-radius: 4px; }
.phone-hero { padding: 18px; border-radius: 18px; }
.phone-badge { min-width: 56px; min-height: 56px; border-radius: 28px; background-color: alpha(currentColor, 0.10); -gtk-icon-size: 28px; }
.phone-title { font-size: 1.3em; font-weight: 700; }
.status-pill { padding: 3px 12px; border-radius: 999px; font-size: 0.85em; font-weight: 600; background-color: alpha(currentColor, 0.14); }
.feature-card { padding: 18px 20px; border-radius: 18px; }
.tool-card { padding: 16px; border-radius: 16px; }
.tool-icon { -gtk-icon-size: 28px; }
.tool-title { font-weight: 700; font-size: 1.05em; }
.icon-choice { padding: 4px; border-radius: 16px; }
.icon-choice:checked { box-shadow: inset 0 0 0 2px var(--accent-bg-color); background: none; }
.color-choice { min-width: 34px; min-height: 34px; padding: 0; box-shadow: inset 0 0 0 1px alpha(currentColor, 0.25); }
.color-choice:checked { box-shadow: inset 0 0 0 3px var(--accent-bg-color); }
)css";

// The Windows palette (App.xaml): accent, accent container, warning, modes.
constexpr const char* kDark = R"css(
:root { --accent-bg-color: #80BFEA; --accent-fg-color: #000000; --accent-color: #80BFEA; }
.mode-off { color: #ACBBD5; } .mode-1 { color: #AAA5FF; } .mode-2 { color: #E7C48E; } .mode-3 { color: #F2A7AB; } scale.mode-3 highlight { background-color: #F2A7AB; } scale.mode-2 highlight { background-color: #E7C48E; }
.accent-text { color: #80BFEA; } .ready-text { color: #8BE0B0; } .warning-text { color: #E3C184; }
.dot.ready { background: #8BE0B0; } .dot.missing { background: #E3C184; }
.power-button.off { background: #303D45; color: #80BFEA; box-shadow: inset 0 0 0 1.5px #80BFEA; }
.power-button.off:hover { background: shade(#303D45, 1.2); }
.power-button.on { background: #80BFEA; color: #000000; box-shadow: none; }
.power-button.on:hover { background: shade(#80BFEA, 1.08); }
.power-button.attention { background: #3B3324; color: #E3C184; box-shadow: inset 0 0 0 1.5px #E3C184; }
.power-button.attention:hover { background: shade(#3B3324, 1.25); }
.status-glyph.off { color: #ACBBD5; } .status-glyph.on { color: #8BE0B0; } .status-glyph.attention { color: #E3C184; } .status-glyph.working { color: #80BFEA; }
.prayer { color: #E7C48E; }
)css";

constexpr const char* kLight = R"css(
:root { --accent-bg-color: #17659B; --accent-fg-color: #FFFFFF; --accent-color: #17659B; }
.mode-off { color: #4F607D; } .mode-1 { color: #5B52C7; } .mode-2 { color: #795315; } .mode-3 { color: #A02C3B; } scale.mode-3 highlight { background-color: #A02C3B; } scale.mode-2 highlight { background-color: #795315; }
.accent-text { color: #17659B; } .ready-text { color: #17613C; } .warning-text { color: #805B20; }
.dot.ready { background: #17613C; } .dot.missing { background: #805B20; }
.power-button.off { background: #DEE6EB; color: #17659B; box-shadow: inset 0 0 0 1.5px #17659B; }
.power-button.off:hover { background: shade(#DEE6EB, 0.95); }
.power-button.on { background: #17659B; color: #FFFFFF; box-shadow: none; }
.power-button.on:hover { background: shade(#17659B, 1.1); }
.power-button.attention { background: #F4ECD9; color: #805B20; box-shadow: inset 0 0 0 1.5px #805B20; }
.power-button.attention:hover { background: shade(#F4ECD9, 0.95); }
.status-glyph.off { color: #4F607D; } .status-glyph.on { color: #17613C; } .status-glyph.attention { color: #805B20; } .status-glyph.working { color: #17659B; }
.prayer { color: #795315; }
)css";

// High contrast keeps libadwaita's own colours; Sukut's states differ by shape and outline only.
constexpr const char* kHighContrast = R"css(
.dot.ready, .dot.missing { background: currentColor; }
.power-button.off, .power-button.attention { box-shadow: inset 0 0 0 2px currentColor; }
.power-button.on { background: var(--accent-bg-color); color: var(--accent-fg-color); }
)css";

const Palette kDarkPalette{{0x80 / 255., 0xBF / 255., 0xEA / 255.}, {0x30 / 255., 0x3D / 255., 0x45 / 255.},
                           {0xE3 / 255., 0xC1 / 255., 0x84 / 255.},
                           {{0xAC / 255., 0xBB / 255., 0xD5 / 255.}, {0xAA / 255., 0xA5 / 255., 1.}, {0xE7 / 255., 0xC4 / 255., 0x8E / 255.},
                            {0xF2 / 255., 0xA7 / 255., 0xAB / 255.}},
                           false};
const Palette kLightPalette{{0x17 / 255., 0x65 / 255., 0x9B / 255.}, {0xDE / 255., 0xE6 / 255., 0xEB / 255.},
                            {0x80 / 255., 0x5B / 255., 0x20 / 255.},
                            {{0x4F / 255., 0x60 / 255., 0x7D / 255.}, {0x5B / 255., 0x52 / 255., 0xC7 / 255.}, {0x79 / 255., 0x53 / 255., 0x15 / 255.},
                             {0xA0 / 255., 0x2C / 255., 0x3B / 255.}},
                            false};

GtkCssProvider* g_palette = nullptr;

void ApplyPalette() {
  AdwStyleManager* manager = adw_style_manager_get_default();
  const bool contrast = adw_style_manager_get_high_contrast(manager);
  const bool dark = adw_style_manager_get_dark(manager);
  std::string css = contrast ? kHighContrast : dark ? kDark : kLight;
  gtk_css_provider_load_from_string(g_palette, css.c_str());
}

}  // namespace

std::filesystem::path DataDir() {
  if (const char* env = std::getenv("SUKUT_DATA_DIR"); env && *env) return env;
  std::error_code code;
  const auto staged = std::filesystem::read_symlink("/proc/self/exe", code).parent_path() / ".." / "share" / "sukut";
  if (std::filesystem::exists(staged / "fonts")) return staged.lexically_normal();
  return InstallPrefix() / "share/sukut";
}

void LoadFonts() {
  FcConfig* config = FcConfigGetCurrent();
  std::error_code code;
  for (const auto& entry : std::filesystem::directory_iterator(DataDir() / "fonts", code))
    if (entry.path().extension() == ".ttf")
      FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(entry.path().c_str()));
}

void InstallStyle() {
  GdkDisplay* display = gdk_display_get_default();
  const auto icons = DataDir() / "icons";
  gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(display), icons.c_str());

  GtkCssProvider* base = gtk_css_provider_new();
  gtk_css_provider_load_from_string(base, kBase);
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(base), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_palette = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(g_palette), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  ApplyPalette();
  AdwStyleManager* manager = adw_style_manager_get_default();
  g_signal_connect(manager, "notify::dark", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer) { ApplyPalette(); }), nullptr);
  g_signal_connect(manager, "notify::high-contrast", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer) { ApplyPalette(); }), nullptr);
}

Palette CurrentPalette() {
  AdwStyleManager* manager = adw_style_manager_get_default();
  Palette p = adw_style_manager_get_dark(manager) ? kDarkPalette : kLightPalette;
  p.high_contrast = adw_style_manager_get_high_contrast(manager);
  return p;
}

}  // namespace sukut::window
