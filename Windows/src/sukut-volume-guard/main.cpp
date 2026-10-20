#include <windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
  int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
  if(!args || count!=5){if(args)LocalFree(args);return 2;}
  wchar_t* end=nullptr;const unsigned long pid=wcstoul(args[1],&end,10);
  if(!pid || *end){LocalFree(args);return 2;}
  const std::wstring engine=args[2],root=args[3],owner=args[4];LocalFree(args);
  const auto suffix=std::to_wstring(pid)+L"-"+owner;
  HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
  HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\SukutVolumeReady-"+suffix).c_str());
  HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\SukutVolumeStop-"+suffix).c_str());
  HMODULE library=LoadLibraryExW(engine.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  using Restore=int(__cdecl*)(const wchar_t*,const wchar_t*);
  const auto restore=library?reinterpret_cast<Restore>(GetProcAddress(library,"Sukut_RestoreRouting")):nullptr;
  int result=2;
  if(parent && ready && stop && restore && SetEvent(ready)){
    HANDLE handles[]={parent,stop};
    const auto reason=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
    if(reason==WAIT_OBJECT_0){
      // A device can be briefly unavailable during a crash/driver restart.
      // Keep the durable journal if all bounded attempts fail.
      result=1;
      for(int attempt=0;attempt<20;++attempt){if(restore(root.c_str(),owner.c_str())){result=0;break;}Sleep(500);}
    }else if(reason==WAIT_OBJECT_0+1)result=0;
  }
  if(library)FreeLibrary(library);if(parent)CloseHandle(parent);
  if(ready)CloseHandle(ready);if(stop)CloseHandle(stop);return result;
}
