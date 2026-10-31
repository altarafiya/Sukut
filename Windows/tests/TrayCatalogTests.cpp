#include "sukut-host/TrayCatalog.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace sukut::host;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
  auto apps=MergeSourceApps({{4,L"Discord"},{9,L"Audio-only app"},{0,L"Invalid"}},{{4,L"duplicate"},{2,L"Microsoft Edge"},{3,L"Silent app"},{5,L"SukutWindow"},{6,L"TextInputHost"},{7,L""}});
  Check(apps.size()==4,"sessions/open windows did not merge correctly");
  Check(apps[0].name==L"Audio-only app" && apps[1].name==L"Discord" && apps[2].pid==2 && apps[3].pid==3,"names/order or session priority changed");
  Check(MergeSourceApps({},{{1,L"Open before playback"}}).size()==1,"silent open app missing without audio sessions");
  Check(MergeSourceApps({{1,L"Audio only"}},{}).size()==1,"audio-only session missing without a main window");
  Check(std::wstring_view(OutputGlyph(L"Speakers (HyperX Cloud III S Wireless)"))==L"\uE7F6","HyperX headphones drawn as speakers");
  Check(std::wstring_view(OutputGlyph(L"USB HEADSET"))==L"\uE95B","headset glyph incorrect");
  Check(std::wstring_view(OutputGlyph(L"AirPods Pro"))==L"\uE7F6","earphone glyph incorrect");
  Check(std::wstring_view(OutputGlyph(L"NVIDIA HDMI"))==L"\uE7F4","display audio glyph incorrect");
  Check(std::wstring_view(OutputGlyph(L"Speakers (Realtek(R) Audio)"))==L"\uE767","speaker glyph incorrect");
  const auto visible=VisibleSourceApps();
  Check(std::none_of(visible.begin(),visible.end(),[](const auto& app){return app.pid==GetCurrentProcessId() || app.name.empty();}),"scan included itself or an unnamed app");
  std::cout<<"PASS tray catalog: silent/open and audio-only apps, PID deduplication, helper exclusion, ordering, endpoint icons and read-only window discovery\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
