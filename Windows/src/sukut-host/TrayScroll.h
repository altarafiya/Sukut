#pragma once
#include <algorithm>
#include <cstdint>

namespace sukut::host {
// Pixel geometry only: the LISTBOX keeps its native selection, keyboard and
// accessibility behavior. Its slim scrollbar needs no window or bitmap cache.
struct TrayScroll {
  int rows = 1, maximum = 0, track = 0, thumb = 0, offset = 0;
  [[nodiscard]] bool Visible() const { return maximum > 0 && track > 0; }
  [[nodiscard]] int TopFromOffset(int y) const {
    const int travel=track-thumb;
    return travel>0?static_cast<int>((static_cast<std::int64_t>(std::clamp(y,0,travel))*maximum+travel/2)/travel):0;
  }
  static TrayScroll Calculate(int count,int height,int row_height,int minimum_thumb,int top,int padding=4) {
    TrayScroll value;
    value.rows=std::max(1,height/std::max(1,row_height));
    value.maximum=std::max(0,count-value.rows);value.track=std::max(0,height-2*padding);
    if(!value.Visible())return value;
    value.thumb=std::clamp(static_cast<int>(static_cast<std::int64_t>(value.track)*value.rows/std::max(1,count)),std::min(minimum_thumb,value.track),value.track);
    value.offset=static_cast<int>((static_cast<std::int64_t>(value.track-value.thumb)*std::clamp(top,0,value.maximum)+value.maximum/2)/value.maximum);
    return value;
  }
};
}
