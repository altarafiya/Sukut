#include "window/AppIcon.h"

#include "audio/Sandbox.h"
#include "window/Style.h"

#include <glib.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace sukut::window::app_icon {
namespace {

constexpr const char* kAppId = "io.github.altarafiya.Sukut";
constexpr int kSizes[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};

std::filesystem::path ConfigFile() { return std::filesystem::path(g_get_user_config_dir()) / "sukut" / "app-icon"; }
std::filesystem::path UserIcons() { return std::filesystem::path(g_get_user_data_dir()) / "icons" / "hicolor"; }

template <typename T> bool Known(const T& list, const std::string& id) {
  for (const auto& item : list) if (id == item.id) return true;
  return false;
}

void Hex(const char* hex, double& r, double& g, double& b) {
  const unsigned value = static_cast<unsigned>(std::strtoul(hex + 1, nullptr, 16));
  r = ((value >> 16) & 0xff) / 255.0; g = ((value >> 8) & 0xff) / 255.0; b = (value & 0xff) / 255.0;
}

}  // namespace

bool CanApply() { return !InSandbox(); }

Choice Load() {
  Choice choice;
  std::ifstream in(ConfigFile());
  std::string mark, color;
  if (in >> mark >> color && Known(kMarks, mark) && Known(kColors, color)) choice = {mark, color};
  return choice;
}

cairo_surface_t* Render(const Choice& choice, int size) {
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(surface);
  // The rounded square: corner radius 22.5 % of the side, as on every platform.
  const double r = size * 0.225;
  cairo_new_sub_path(cr);
  cairo_arc(cr, size - r, r, r, -M_PI / 2, 0);
  cairo_arc(cr, size - r, size - r, r, 0, M_PI / 2);
  cairo_arc(cr, r, size - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, r, r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
  double red = 0, green = 0, blue = 0;
  for (const auto& c : kColors) if (choice.color == c.id) Hex(c.hex, red, green, blue);
  cairo_set_source_rgb(cr, red, green, blue);
  cairo_fill(cr);
  // The mark fits a centred box 72 % of the side.
  const auto file = DataDir() / "icons" / "marks" / (choice.mark + ".png");
  cairo_surface_t* mark = cairo_image_surface_create_from_png(file.c_str());
  if (cairo_surface_status(mark) == CAIRO_STATUS_SUCCESS) {
    const double w = cairo_image_surface_get_width(mark), h = cairo_image_surface_get_height(mark);
    const double scale = std::min(size * 0.72 / w, size * 0.72 / h);
    cairo_translate(cr, (size - w * scale) / 2, (size - h * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, mark, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
  }
  cairo_surface_destroy(mark);
  cairo_destroy(cr);
  return surface;
}

bool Apply(const Choice& choice, std::string& error) {
  if (!CanApply()) { error = "sandboxed"; return false; }
  std::error_code code;
  std::filesystem::create_directories(ConfigFile().parent_path(), code);
  { std::ofstream(ConfigFile()) << choice.mark << ' ' << choice.color << '\n'; }
  const bool standard = choice == Choice{};
  for (const int size : kSizes) {
    const auto dir = UserIcons() / (std::to_string(size) + "x" + std::to_string(size)) / "apps";
    const auto file = dir / (std::string(kAppId) + ".png");
    // The default needs no override: the system copy is the same picture.
    if (standard) { std::filesystem::remove(file, code); continue; }
    std::filesystem::create_directories(dir, code);
    cairo_surface_t* icon = Render(choice, size);
    const cairo_status_t status = cairo_surface_write_to_png(icon, file.c_str());
    cairo_surface_destroy(icon);
    if (status != CAIRO_STATUS_SUCCESS) { error = "cannot write " + file.string(); return false; }
  }
  // A newer theme folder tells desktops to look again.
  std::filesystem::last_write_time(UserIcons(), std::filesystem::file_time_type::clock::now(), code);
  return true;
}

}  // namespace sukut::window::app_icon
