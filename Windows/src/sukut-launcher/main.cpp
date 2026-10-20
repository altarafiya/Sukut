#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdint>
#include <string>
#include <filesystem>
#ifndef SUKUT_PORTABLE_LAUNCHER
#include "AppPath.h"
#endif

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
  const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#ifdef SUKUT_PORTABLE_LAUNCHER
  wchar_t executable[32768];
  const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
  if(length==0 || length>=32768)return 1;
  const auto root=std::filesystem::path(executable).parent_path();
  std::wstring app=(root/L"app/Sukut.exe").wstring(),directory=root.wstring();
#else
  std::wstring app=SUKUT_APP_PATH,directory=SUKUT_WORKDIR;
#endif
  for(auto& c:app)if(c==L'/')c=L'\\';
  for(auto& c:directory)if(c==L'/')c=L'\\';
  const auto result=reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr,L"open",app.c_str(),nullptr,directory.c_str(),SW_SHOWNORMAL));
  if(SUCCEEDED(initialized))CoUninitialize();
  if(result>32)return 0;
  MessageBoxW(nullptr,L"Sukut could not open. Extract the complete package and keep the app folder beside Sukut.exe. For a Desktop project launcher, keep its Windows project in place.",L"\u0633\u064f\u0643\u0648\u062a Sukut",MB_OK|MB_ICONERROR);
  return 1;
}
