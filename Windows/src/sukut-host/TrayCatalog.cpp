#include "sukut-host/TrayCatalog.h"
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_set>
#include <utility>

namespace sukut::host {
namespace {
bool Equal(std::wstring_view a,std::wstring_view b) {
  return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](wchar_t x,wchar_t y){return std::towlower(x)==std::towlower(y);});
}
bool Contains(std::wstring_view name,std::wstring_view part) {
  return std::search(name.begin(),name.end(),part.begin(),part.end(),[](wchar_t x,wchar_t y){return std::towlower(x)==std::towlower(y);})!=name.end();
}
bool Excluded(std::wstring_view name) {
  for(auto helper:{L"Sukut",L"SukutWindow",L"explorer",L"ApplicationFrameHost",L"SystemSettings",L"TextInputHost",L"codex-computer-use"})
    if(Equal(name,helper))return true;
  return false;
}
std::wstring DisplayName(std::wstring name) {
  for(auto [exe,label]:{std::pair{L"msedge",L"Microsoft Edge"},std::pair{L"chrome",L"Google Chrome"},
      std::pair{L"firefox",L"Firefox"},std::pair{L"steam",L"Steam"},std::pair{L"steamwebhelper",L"Steam"},std::pair{L"ProtonVPN.Client",L"Proton VPN"}})
    if(Equal(name,exe))return label;
  return name;
}
struct Scan { DWORD session=0;std::unordered_set<DWORD> seen;std::vector<SourceAppChoice> apps; };
BOOL CALLBACK AddWindow(HWND window,LPARAM argument) {
  auto& scan=*reinterpret_cast<Scan*>(argument);
  if(!IsWindowVisible(window) || GetWindow(window,GW_OWNER) || GetWindowTextLengthW(window)==0 ||
     (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
  DWORD cloaked=0;
  if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked)return TRUE;
  DWORD pid=0,session=0;GetWindowThreadProcessId(window,&pid);
  if(!pid || pid==GetCurrentProcessId() || !ProcessIdToSessionId(pid,&session) || session!=scan.session || !scan.seen.insert(pid).second)return TRUE;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if(!process)return TRUE;
  std::wstring path(512,L'\0');DWORD size=static_cast<DWORD>(path.size());
  BOOL okay=QueryFullProcessImageNameW(process,0,path.data(),&size);
  if(!okay && GetLastError()==ERROR_INSUFFICIENT_BUFFER){path.resize(32768);size=static_cast<DWORD>(path.size());okay=QueryFullProcessImageNameW(process,0,path.data(),&size);}
  CloseHandle(process);
  if(okay){path.resize(size);auto name=std::filesystem::path(path).stem().wstring();if(!Excluded(name))scan.apps.push_back({pid,DisplayName(std::move(name))});}
  return TRUE;
}
}  // namespace
const wchar_t* OutputGlyph(std::wstring_view name) {
  if(Contains(name,L"headset"))return L"\uE95B";
  for(auto part:{L"head",L"HyperX",L"earphone",L"buds",L"AirPods"})if(Contains(name,part))return L"\uE7F6";
  for(auto part:{L"HDMI",L"DisplayPort",L"NVIDIA",L"Monitor"})if(Contains(name,part))return L"\uE7F4";
  return L"\uE767";
}
std::vector<SourceAppChoice> VisibleSourceApps() {
  Scan scan;if(ProcessIdToSessionId(GetCurrentProcessId(),&scan.session))EnumWindows(AddWindow,reinterpret_cast<LPARAM>(&scan));
  return std::move(scan.apps);
}
std::vector<SourceAppChoice> MergeSourceApps(const std::vector<SourceAppChoice>& sessions,const std::vector<SourceAppChoice>& windows) {
  std::unordered_set<std::uint32_t> seen;std::vector<SourceAppChoice> result;
  for(const auto* list:{&sessions,&windows})for(const auto& app:*list)
    if(app.pid && !app.name.empty() && !Excluded(app.name) && seen.insert(app.pid).second)result.push_back(app);
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return CompareStringOrdinal(a.name.c_str(),-1,b.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;});
  return result;
}
}  // namespace sukut::host
