#pragma once

// Sukut's look on top of libadwaita (Linux/PLAN.md section 7): its own fonts,
// the palette from Windows (accent, mode colours, warning) for light, dark and
// high contrast, and its icons. The palette follows AdwStyleManager.
#include <filesystem>

#include <adwaita.h>

namespace sukut::window {

// <build>/share/sukut beside <build>/bin in a development build, else /usr/share/sukut.
std::filesystem::path DataDir();

// Readex Pro, IBM Plex Sans Arabic and Handjet, for this process only. Call before GTK starts.
void LoadFonts();

// CSS and icons for the default display. Call once, after adw_init.
void InstallStyle();

struct Palette {
  double accent[3], accent_container[3], warning[3], modes[4][3];  // modes: off, music filter, optimized, strict
  bool high_contrast;
};
// The colours in effect now, for widgets that draw themselves (the Standby face, the credit).
Palette CurrentPalette();

}  // namespace sukut::window
