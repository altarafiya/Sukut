#pragma once
#include <algorithm>
namespace sukut::window {
// Coordinates are logical pixels; reserve a gap inside the compositor's work area.
inline int TrayLeft(int anchor_x,int available_width,int menu_width) {
  return std::clamp(anchor_x-menu_width/2,8,std::max(8,available_width-menu_width-8));
}
}
