#pragma once
#include <windows.h>
#include <algorithm>

namespace sukut::host {
inline UINT32 ModeColor(int mode,bool dark) {
  switch(mode) {
    case 1:return dark?0xAAA5FF:0x5B52C7;
    case 2:return dark?0xE7C48E:0x795315;
    case 3:return dark?0xF2A7AB:0xA02C3B;
    default:return dark?0xACBBD5:0x4F607D;
  }
}
inline UINT32 ModeWash(UINT32 base,UINT32 accent) {
  UINT32 rgb=0;
  for(int shift:{0,8,16})rgb|=(((((base>>shift)&255)*82+((accent>>shift)&255)*18)/100)<<shift);
  return rgb;
}
// Tint the existing transparent Sukut glyph, preserving its alpha coverage.
// Only one small mode icon is retained by the host; no image cache/runtime.
inline HICON TintIcon(HICON source,UINT32 rgb) {
  ICONINFO icon{};if(!source || !GetIconInfo(source,&icon))return nullptr;
  HICON result=nullptr;
  if(icon.hbmColor) {
    BITMAP bitmap{};GetObjectW(icon.hbmColor,sizeof(bitmap),&bitmap);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),bitmap.bmWidth,-bitmap.bmHeight,1,32,BI_RGB,0,0,0,0,0};
    void* bits=nullptr;HDC dc=GetDC(nullptr);
    HBITMAP colored=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(colored && bits && GetDIBits(dc,icon.hbmColor,0,bitmap.bmHeight,bits,&info,DIB_RGB_COLORS)) {
      auto* pixels=static_cast<DWORD*>(bits);
      for(int i=0;i<bitmap.bmWidth*bitmap.bmHeight;++i) {
        const DWORD a=pixels[i]>>24;
        pixels[i]=(a<<24)|(((((rgb>>16)&255)*a+127)/255)<<16)|(((((rgb>>8)&255)*a+127)/255)<<8)|(((rgb&255)*a+127)/255);
      }
      ICONINFO replacement=icon;replacement.hbmColor=colored;result=CreateIconIndirect(&replacement);
    }
    if(colored)DeleteObject(colored);ReleaseDC(nullptr,dc);
  }
  if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
  return result?result:CopyIcon(source);
}
}
