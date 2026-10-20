#include "sukut/AudioRouting.h"
#include "audio/WinAudio.h"
#include "audio/AudioEndpoints.h"
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <shobjidl.h>
#include <endpointvolume.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <array>
#include <cmath>

using cleanwave::sukut::AudioRouting;
namespace {
void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
std::string Read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::string Utf8(const std::wstring& text){
  int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
  std::string value(n,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),value.data(),n,nullptr,nullptr);return value;
}
void Journal(const std::filesystem::path& file,unsigned pid,std::uint64_t started,const std::wstring& path){
  std::ofstream out(file,std::ios::binary);
  // Fake endpoint IDs and no owned volume/defaults: this fixture cannot modify
  // system endpoints. Its route never equals any real persisted audio endpoint.
  out<<"version=4\npid="<<pid<<"\nstarted="<<started<<"\npath="<<Utf8(path)
     <<"\ncable={SUKUT-TEST-NONEXISTENT}\nphysical={SUKUT-TEST-PHYSICAL}"
       "\nconsole=prior-console\nmultimedia=prior-multimedia\ndefaults_owned=0"
       "\ndefault_console=\ndefault_multimedia=\nowner=test-owner\nvolume_owned=0\nphysical_volume_owned=0\n";
}
// Test-only view of the OS policy ABI. The fixture writes only its own app's
// route; all endpoint/default/volume changes are restored before it exits.
struct __declspec(uuid("ab3d4648-e242-459f-b02f-541c70306324")) AppPolicy : IInspectable {
  virtual HRESULT STDMETHODCALLTYPE Slot0()=0; virtual HRESULT STDMETHODCALLTYPE Slot1()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot2()=0; virtual HRESULT STDMETHODCALLTYPE Slot3()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot4()=0; virtual HRESULT STDMETHODCALLTYPE Slot5()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot6()=0; virtual HRESULT STDMETHODCALLTYPE Slot7()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot8()=0; virtual HRESULT STDMETHODCALLTYPE Slot9()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot10()=0; virtual HRESULT STDMETHODCALLTYPE Slot11()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot12()=0; virtual HRESULT STDMETHODCALLTYPE Slot13()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot14()=0; virtual HRESULT STDMETHODCALLTYPE Slot15()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot16()=0; virtual HRESULT STDMETHODCALLTYPE Slot17()=0;
  virtual HRESULT STDMETHODCALLTYPE Slot18()=0;
  virtual HRESULT STDMETHODCALLTYPE SetPersisted(unsigned,EDataFlow,ERole,HSTRING)=0;
  virtual HRESULT STDMETHODCALLTYPE GetPersisted(unsigned,EDataFlow,ERole,HSTRING*)=0;
};
std::wstring AppId(const std::wstring& id){return id.empty()?L"":L"\\\\?\\SWD#MMDEVAPI#"+id+L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";}
std::wstring Default(ERole role){
  using namespace cleanwave::audio_detail;auto e=Enumerator();ComPtr<IMMDevice> d;
  CheckHr(e->GetDefaultAudioEndpoint(eRender,role,&d),"fixture default endpoint");wchar_t* id=nullptr;
  CheckHr(d->GetId(&id),"fixture endpoint id");std::wstring result(id);CoTaskMemFree(id);return result;
}
std::array<std::wstring,2> ReadApp(AppPolicy* policy){
  std::array<std::wstring,2> result;
  for(int r=0;r<2;++r){HSTRING value=nullptr;const auto hr=policy->GetPersisted(GetCurrentProcessId(),eRender,static_cast<ERole>(r),&value);
    if(hr!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))cleanwave::audio_detail::CheckHr(hr,"fixture read app route");
    UINT32 n=0;const auto text=WindowsGetStringRawBuffer(value,&n);result[r].assign(text,n);if(value)WindowsDeleteString(value);}
  return result;
}
void WriteApp(AppPolicy* policy,const std::array<std::wstring,2>& routes){
  for(int r=0;r<2;++r){HSTRING value=nullptr;if(!routes[r].empty())cleanwave::audio_detail::CheckHr(WindowsCreateString(routes[r].c_str(),static_cast<UINT32>(routes[r].size()),&value),"fixture route string");
    const auto hr=policy->SetPersisted(GetCurrentProcessId(),eRender,static_cast<ERole>(r),value);if(value)WindowsDeleteString(value);
    cleanwave::audio_detail::CheckHr(hr,"fixture write own app route");}
}
bool AppUses(AppPolicy* policy,const std::wstring& id){
  const auto routes=ReadApp(policy);return _wcsicmp(routes[0].c_str(),id.c_str())==0 && _wcsicmp(routes[1].c_str(),id.c_str())==0;
}
std::pair<float,BOOL> Volume(const std::wstring& id){
  using namespace cleanwave::audio_detail;auto device=Device(id);ComPtr<IAudioEndpointVolume> value;
  CheckHr(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&value),"fixture master volume");
  float level=0;BOOL mute=FALSE;CheckHr(value->GetMasterVolumeLevelScalar(&level),"fixture read volume");CheckHr(value->GetMute(&mute),"fixture read mute");return {level,mute};
}
void Handoff(const std::filesystem::path& root){
  using namespace cleanwave;using namespace cleanwave::audio_detail;
  Check(!std::filesystem::exists(root),"Use a fresh evidence directory");std::filesystem::create_directories(root);
  CheckHr(RoInitialize(RO_INIT_SINGLETHREADED),"fixture runtime");
  struct Runtime {~Runtime(){RoUninitialize();}} runtime;
  const auto mutex=CreateMutexW(nullptr,FALSE,L"Local\\CleanWaveWindowsDesktop");
  const auto exists=GetLastError()==ERROR_ALREADY_EXISTS;
  struct Lease {HANDLE value;~Lease(){if(value)CloseHandle(value);}} lease{mutex};
  Check(mutex && !exists,"Stop the active Sukut bridge before the silent routing fixture");
  CheckHr(SetCurrentProcessExplicitAppUserModelID(L"Sukut.SourceHandoffFixture"),"fixture app identity");
  const std::array<std::wstring,2> defaults={Default(eConsole),Default(eMultimedia)};
  Check(defaults[0]==defaults[1],"Fixture requires matching console/media defaults to leave OFF's endpoint choice unchanged");
  const auto physical=defaults[0];std::wstring cable;
  for(const auto& endpoint:EnumerateAudioEndpoints())if(endpoint.flow==EndpointFlow::Render && endpoint.state==1 && endpoint.name.find(L"CABLE Input")!=std::wstring::npos)cable=endpoint.id;
  Check(!cable.empty() && cable!=physical,"Stop Sukut and select a physical Windows output before this fixture");
  const auto cable_before=Volume(cable),physical_before=Volume(physical);
  ComPtr<IMMDevice> device=Device(physical);ComPtr<IAudioClient> client;
  CheckHr(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client),"fixture silent client");
  WAVEFORMATEX* format=nullptr;CheckHr(client->GetMixFormat(&format),"fixture format");
  auto initialized=client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,format,nullptr);CoTaskMemFree(format);CheckHr(initialized,"fixture silent session");
  UINT32 frames=0;CheckHr(client->GetBufferSize(&frames),"fixture silence frames");ComPtr<IAudioRenderClient> render;
  CheckHr(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(render.GetAddressOf())),"fixture renderer");
  BYTE* data=nullptr;CheckHr(render->GetBuffer(frames,&data),"fixture silence buffer");CheckHr(render->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT),"fixture silent buffer");
  CheckHr(client->Start(),"fixture silent session start");
  constexpr wchar_t policy_name[]=L"Windows.Media.Internal.AudioPolicyConfig";
  HSTRING name=nullptr;CheckHr(WindowsCreateString(policy_name,static_cast<UINT32>(std::size(policy_name)-1),&name),"fixture policy name");
  ComPtr<AppPolicy> policy;auto activated=RoGetActivationFactory(name,__uuidof(AppPolicy),reinterpret_cast<void**>(policy.GetAddressOf()));WindowsDeleteString(name);CheckHr(activated,"fixture policy");
  const auto before=ReadApp(policy.Get());
  const auto journal=root/L"route.ini";auto route=std::make_unique<AudioRouting>(journal);
  try{
    WriteApp(policy.Get(),{AppId(physical),AppId(physical)});
    route->Begin(cable,physical,GetCurrentProcessId(),L"fixture");
    Check(AppUses(policy.Get(),AppId(cable)),"source mode did not route the fixture to cable");
    route->Restore();
    Check(AppUses(policy.Get(),AppId(physical)),"specific source restoration lost its old physical pin");
    route->Begin(cable,physical,0,L"fixture");
    Check(Default(eConsole)==cable && Default(eMultimedia)==cable,"All system did not select cable");
    Check(AppUses(policy.Get(),AppId(cable)),"previous source bypassed All system");
    Check(Read(journal).find("version=5\nall_system=1\npid="+std::to_string(GetCurrentProcessId()))!=std::string::npos,"global handoff not journaled");
    route->CheckRoute();route->Restore();
    Check(Default(eConsole)==defaults[0] && Default(eMultimedia)==defaults[1],"global Stop did not restore Windows defaults");
    Check(AppUses(policy.Get(),AppId(physical)),"global Stop did not restore exact app pin");
    route->Begin(cable,physical,0,L"fixture");
    AudioRouting watchdog(journal);watchdog.Restore(L"fixture");route.reset();
    Check(Default(eConsole)==defaults[0] && Default(eMultimedia)==defaults[1],"watchdog did not restore global defaults");
    Check(AppUses(policy.Get(),AppId(physical)),"watchdog did not restore exact app pin");
    cleanwave::sukut::SelectPhysicalOutput(cable,physical,GetCurrentProcessId());
    Check(AppUses(policy.Get(),L""),"OFF left a physical app pin instead of following the selected default");
    WriteApp(policy.Get(),before);client->Stop();
    const auto cable_after=Volume(cable),physical_after=Volume(physical);
    Check(std::abs(cable_before.first-cable_after.first)<.00001f && cable_before.second==cable_after.second,"fixture did not restore cable volume/mute");
    Check(std::abs(physical_before.first-physical_after.first)<.00001f && physical_before.second==physical_after.second,"fixture changed physical volume/mute");
    std::cout<<"PASS source -> All system includes prior source; exact app/default restoration; v5 watchdog restoration; OFF follows selected default. Silent fixture only.\n";
  }catch(...){try{if(route)route->Restore();WriteApp(policy.Get(),before);client->Stop();}catch(...){}throw;}
}
void Unresolved(unsigned pid,const std::filesystem::path& root){
  using namespace cleanwave::audio_detail;
  Check(!std::filesystem::exists(root),"Use a fresh evidence directory");std::filesystem::create_directories(root);
  CheckHr(RoInitialize(RO_INIT_SINGLETHREADED),"fixture runtime");
  struct Runtime {~Runtime(){RoUninitialize();}} runtime;
  const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  Check(process!=nullptr,"unresolved fixture process is no longer live");
  wchar_t image[32768];DWORD size=std::size(image);FILETIME created,exit,kernel,user;
  const bool identity=QueryFullProcessImageNameW(process,0,image,&size) && GetProcessTimes(process,&created,&exit,&kernel,&user);CloseHandle(process);
  Check(identity,"cannot inspect unresolved process identity");
  const auto started=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
  HSTRING name=nullptr;constexpr wchar_t factory[]=L"Windows.Media.Internal.AudioPolicyConfig";
  CheckHr(WindowsCreateString(factory,std::size(factory)-1,&name),"fixture policy name");ComPtr<AppPolicy> policy;
  auto hr=RoGetActivationFactory(name,__uuidof(AppPolicy),reinterpret_cast<void**>(policy.GetAddressOf()));WindowsDeleteString(name);CheckHr(hr,"fixture policy");
  HSTRING value=nullptr;hr=policy->GetPersisted(pid,eRender,eConsole,&value);if(value)WindowsDeleteString(value);
  Check(hr==E_INVALIDARG,"This PID does not currently reproduce the unresolved audio identity");
  const auto journal=root/L"sukut-routing.ini";Journal(journal,pid,started,{image,size});
  AudioRouting route(journal);route.Restore(L"test-owner");
  Check(!std::filesystem::exists(journal),"unresolved live source blocked the main bridge journal");
  const auto pending=root/L"pending-app-routes";
  Check(std::filesystem::is_directory(pending) && !std::filesystem::is_empty(pending),"unresolved live source lost its exact saved preferences");
  const auto record=std::filesystem::directory_iterator(pending)->path();
  const auto text=Read(record);
  Check(text.find("console=prior-console")!=std::string::npos && text.find("multimedia=prior-multimedia")!=std::string::npos,"unresolved source preferences lost");
  route.RestorePending();Check(std::filesystem::exists(record),"unresolved source record consumed on retry");
  std::cout<<"PASS E_INVALIDARG live identity releases main lease; exact app preferences deferred and retained. Read-only real policy; fake endpoints, no endpoint writes.\n";
}
}
int wmain(int argc,wchar_t** argv){
  try{
    if(argc==4 && std::wstring(argv[1])==L"--unresolved"){Unresolved(std::stoul(argv[2]),std::filesystem::absolute(argv[3]));return 0;}
    if(argc==3 && std::wstring(argv[1])==L"--handoff"){Handoff(std::filesystem::absolute(argv[2]));return 0;}
    if(argc==2 && std::wstring(argv[1])==L"--idle-child"){Sleep(2500);return 0;}
    if(argc!=2)throw std::runtime_error("usage: sukut_routing_verify new-evidence-directory");
    const auto root=std::filesystem::absolute(argv[1]);
    if(std::filesystem::exists(root))throw std::runtime_error("Use a fresh evidence directory");
    std::filesystem::create_directories(root);
    const auto main=root/L"sukut-routing.ini",pending=root/L"pending-app-routes";
    Journal(main,0xffffffff,1,L"C:\\Sukut-test-absent\\source.exe");
    const auto before=Read(main);
    AudioRouting route(main);route.Restore(L"different-owner");
    Check(Read(main)==before,"wrong owner changed the restoration journal");
    route.Restore(L"test-owner");
    Check(!std::filesystem::exists(main),"absent source kept the main bridge locked");
    Check(std::filesystem::is_directory(pending),"missing deferred journal directory");
    auto first=std::filesystem::directory_iterator(pending)->path();
    auto text=Read(first);
    Check(text.find("console=prior-console")!=std::string::npos && text.find("multimedia=prior-multimedia")!=std::string::npos,"prior app routes lost");
    Check(text.find("defaults_owned=0")!=std::string::npos && text.find("volume_owned=0")!=std::string::npos,"pending record still owns global audio settings");
    route.RestorePending();Check(Read(first)==text,"absent app record was dropped or rewritten");
    Journal(main,0,0,L"");
    AudioRouting next(main);next.Restore();
    Check(!std::filesystem::exists(main) && std::filesystem::exists(first),"unrelated route cannot restore independently");
    std::ofstream(pending/L"malformed.ini")<<"version=99\n";
    route.RestorePending();Check(std::filesystem::exists(pending/L"malformed.ini"),"malformed record lost instead of retained");
    wchar_t image[32768];DWORD length=GetModuleFileNameW(nullptr,image,32768);
    Check(length && length<32768,"cannot identify regression process");
    FILETIME created,exited,kernel,user;
    Check(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"cannot read regression identity");
    const auto started=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    const auto live=pending/L"live.ini";
    Journal(live,GetCurrentProcessId(),started,image);
    route.RestorePending(GetCurrentProcessId());
    Check(std::filesystem::exists(live),"pending cleanup touched the actively filtered app");
    route.RestorePending(0,true);
    Check(std::filesystem::exists(live),"All system cleanup restored a physical pin and removed an app from the global mix");
    route.RestorePending();
    Check(!std::filesystem::exists(live),"live matching app did not finish deferred restoration");
    std::wstring command=L"\""+std::wstring(image)+L"\" --idle-child";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    Check(CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"cannot create process-restoration fixture");
    CloseHandle(child.hThread);
    Check(GetProcessTimes(child.hProcess,&created,&exited,&kernel,&user)!=0,"cannot read child identity");
    const auto child_started=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    const auto reopened=pending/L"reopened-before-audio.ini";
    Journal(reopened,0xffffffff,1,image);
    auto candidates=cleanwave::sukut::LiveSourceProcesses(image);
    Check(std::any_of(candidates.begin(),candidates.end(),[&](const auto& app){return app.pid==child.dwProcessId;}),"reopened process without an audio session was not found");
    route.RestorePending();
    // Windows can reject persisted-route lookup before a first audio session;
    // in that case the journal must remain for the next retry, not be dropped.
    Check(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0,"fixture child did not exit");
    // Keep the process object open: Windows can still resolve its image/PID,
    // but an exited process must not consume the deferred restoration record.
    const auto dead=pending/L"exited-process.ini";
    Journal(dead,child.dwProcessId,child_started,image);
    route.RestorePending();
    Check(std::filesystem::exists(dead),"exited process was mistaken for a reopened app");
    candidates=cleanwave::sukut::LiveSourceProcesses(image);
    Check(std::none_of(candidates.begin(),candidates.end(),[&](const auto& app){return app.pid==child.dwProcessId;}),"exited process is still offered as a live source");
    CloseHandle(child.hProcess);
    std::cout<<"PASS owner guard; exact deferred routes; absent/malformed records retained; active app protected; live app cleanup; reopened app before audio; exited process rejected. No endpoint writes.\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
