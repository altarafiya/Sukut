#pragma once
#include <windows.h>
#include <winternl.h>

namespace sukut::host {
// Keep a real HMENU: no WinUI window, owner-drawn menu, image cache or worker.
// Windows 10 1903+ theme exports are resolved conditionally, as in PowerToys.
inline void ThemeNativeMenu(HWND owner, bool dark) {
  struct ThemeApi {
    using AppMode=int(WINAPI*)(int);
    using WindowMode=bool(WINAPI*)(HWND,bool);
    using Flush=void(WINAPI*)();
    HMODULE library=nullptr; AppMode app=nullptr; WindowMode window=nullptr; Flush flush=nullptr;
    ThemeApi() {
      using VersionFn=LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
      auto version=reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
      RTL_OSVERSIONINFOW info{sizeof(info)};
      if(!version || version(&info)!=0 || info.dwBuildNumber<18362) return;
      library=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
      if(!library) return;
      app=reinterpret_cast<AppMode>(GetProcAddress(library,MAKEINTRESOURCEA(135)));
      window=reinterpret_cast<WindowMode>(GetProcAddress(library,MAKEINTRESOURCEA(133)));
      flush=reinterpret_cast<Flush>(GetProcAddress(library,MAKEINTRESOURCEA(136)));
    }
    ~ThemeApi(){if(library)FreeLibrary(library);}
  };
  static ThemeApi theme;
  HIGHCONTRASTW contrast{sizeof(contrast)};
  if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON)) dark=false;
  if(theme.app) theme.app(dark?2:3); // ForceDark / ForceLight, this native process only.
  if(theme.window) theme.window(owner,dark);
  if(theme.flush) theme.flush();
}
}
