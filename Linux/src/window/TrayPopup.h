#pragma once
#include <string>
namespace sukut::window {
// Separate on-demand UI process. Capture uses a fixed model and never connects audio.
bool TrayPopupSupported();
int RunTrayPopup(const std::string& host_command, int x, int y, const std::string& capture_directory = {}, const std::string& snapshot = {}, bool exercise = false);
}
