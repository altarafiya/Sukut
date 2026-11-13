#pragma once

// The app icon the user picks: one of five marks on a rounded square of one of six
// colours (tools/export-branding.py, Android's AppIcon.kt and Windows' AppIcons.cs
// use the same ids, in the same order). Default: "blue" on "black".
// Outside a sandbox the chosen icon is written to the user's own icon theme
// (~/.local/share/icons/hicolor), which the desktop prefers over the system copy,
// so the menu, taskbar and window use it. Flatpak and Snap can't write there.
#include <array>
#include <cairo.h>
#include <string>

namespace sukut::window::app_icon {

struct Choice {
  std::string mark = "blue";
  std::string color = "black";
  bool operator==(const Choice&) const = default;
};

struct Color { const char* id; const char* hex; const char* en; const char* ar; };
struct Mark { const char* id; const char* en; const char* ar; };
inline constexpr std::array<Mark, 5> kMarks{{{"soft", "Seen", "حرف السين"}, {"wave", "Seen wave", "سين متموّجة"},
                                             {"wordmark", "Sukut wordmark", "كلمة سُكوت"}, {"gold", "Original gold", "الذهبي الأصلي"},
                                             {"blue", "White and blue", "أبيض وأزرق"}}};
inline constexpr std::array<Color, 6> kColors{{{"black", "#000000", "Black", "أسود"}, {"graphite", "#2B2E33", "Graphite", "رمادي داكن"},
                                               {"navy", "#0E2238", "Navy", "كحلي"}, {"forest", "#0F3B2E", "Forest green", "أخضر داكن"},
                                               {"burgundy", "#4A1420", "Burgundy", "عنّابي"}, {"plum", "#2E1A47", "Plum", "بنفسجي"}}};

Choice Load();
// Draws the rounded-square icon at size × size (caller destroys the surface).
cairo_surface_t* Render(const Choice& choice, int size);
// Saves the choice and updates the user's icon theme. False with a reason when it can't.
bool Apply(const Choice& choice, std::string& error);
bool CanApply();  // false in Flatpak and Snap

}  // namespace sukut::window::app_icon
