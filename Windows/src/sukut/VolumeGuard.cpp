#include "audio/WinAudio.h"
#include "sukut/VolumeGuard.h"
#include <vector>

namespace cleanwave::sukut {
namespace {
std::wstring Argument(const std::wstring& text) {
  std::wstring result=L"\"";unsigned slashes=0;
  for(wchar_t c:text){
    if(c==L'\\'){++slashes;continue;}
    result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
  }
  result.append(slashes*2,L'\\');return result+L'\"';
}
}
VolumeGuard::VolumeGuard(const std::filesystem::path& root) {
  GUID id;audio_detail::CheckHr(CoCreateGuid(&id),"create volume-restore owner");
  wchar_t guid[40];StringFromGUID2(id,guid,40);owner_=guid;
  const auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+owner_;
  const auto ready_name=L"Local\\SukutVolumeReady-"+suffix;
  const auto stop_name=L"Local\\SukutVolumeStop-"+suffix;
  HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,ready_name.c_str());
  stop_=CreateEventW(nullptr,TRUE,FALSE,stop_name.c_str());
  try {
    if(!ready || !stop_)throw std::runtime_error("cannot create volume-restore events");
    wchar_t path[32768];const auto module=GetModuleHandleW(L"SukutEngine.dll");
    const DWORD length=GetModuleFileNameW(module,path,32768);
    if(!module || !length || length>=32768)throw std::runtime_error("cannot locate volume-restore runtime");
    const auto engine=std::filesystem::path(path);
    const auto executable=engine.parent_path()/L"SukutVolumeGuard.exe";
    auto command=Argument(executable.wstring())+L" "+std::to_wstring(GetCurrentProcessId())+L" "+
      Argument(engine.wstring())+L" "+Argument(std::filesystem::absolute(root).wstring())+L" "+Argument(owner_);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
                       engine.parent_path().c_str(),&startup,&child))
      throw std::runtime_error("cannot start Sukut volume restoration guard");
    process_=child.hProcess;CloseHandle(child.hThread);
    HANDLE handles[]={ready,process_};
    if(WaitForMultipleObjects(2,handles,FALSE,5000)!=WAIT_OBJECT_0)
      throw std::runtime_error("Sukut volume restoration guard did not become ready");
    CloseHandle(ready);ready=nullptr;Check();
  }catch(...){
    if(stop_)SetEvent(stop_);
    if(process_){WaitForSingleObject(process_,2000);CloseHandle(process_);process_=nullptr;}
    if(stop_){CloseHandle(stop_);stop_=nullptr;}
    if(ready)CloseHandle(ready);throw;
  }
}
VolumeGuard::~VolumeGuard(){
  // If restoration failed, leave the helper waiting for this process to exit.
  // Its owner token prevents an old guard from touching a newer route journal.
  if(process_)CloseHandle(process_);if(stop_)CloseHandle(stop_);
}
void VolumeGuard::Check() const {
  if(!process_ || WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)
    throw std::runtime_error("Sukut volume restoration guard stopped unexpectedly");
}
void VolumeGuard::Stop(){
  if(!process_)return;
  if(!SetEvent(stop_) || WaitForSingleObject(process_,3000)!=WAIT_OBJECT_0)
    throw std::runtime_error("Sukut volume restoration guard is still closing");
  CloseHandle(process_);process_=nullptr;CloseHandle(stop_);stop_=nullptr;
}
}
