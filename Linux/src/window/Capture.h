#pragma once

// The window's test mode (stage 6): renders each page and state to PNG with
// GTK's own snapshot (never a screen capture) and walks the keyboard focus
// order. It uses a fixed model and no host, so nothing touches audio.
//   sukut --capture DIR [--capture-set main|contrast]
#include <string>

#include <adwaita.h>

namespace sukut::window {

int RunCapture(AdwApplication* app, const std::string& directory, const std::string& set);

}  // namespace sukut::window
