#include "audio/WinAudio.h"
#include "audio/AudioEndpoints.h"
#include "sukut/AudioRouting.h"
#include <audiopolicy.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <endpointvolume.h>
#include <tlhelp32.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <optional>
#include <fstream>
#include <map>
#include <set>
#include "desktop/DesktopPreferences.h"

namespace cleanwave::sukut {
using namespace audio_detail;
namespace {
class RuntimeApartment final {
 public:
  RuntimeApartment(){CheckHr(RoInitialize(RO_INIT_SINGLETHREADED),"initialize audio-policy runtime");}
  ~RuntimeApartment(){RoUninitialize();}
};
class RestoreLock final {
 public:
  RestoreLock(){
    mutex_=CreateMutexW(nullptr,FALSE,L"Local\\SukutVolumeRestoration");
    if(!mutex_)throw std::runtime_error("cannot create audio restoration lock");
    const DWORD result=WaitForSingleObject(mutex_,5000);
    if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED){CloseHandle(mutex_);mutex_=nullptr;throw std::runtime_error("audio restoration is busy");}
  }
  ~RestoreLock(){ReleaseMutex(mutex_);CloseHandle(mutex_);}
 private:HANDLE mutex_=nullptr;
};
// These policy interfaces are Windows internal APIs, as used by
// AudioDeviceCmdlets and EarTrumpet. Only endpoint routing methods are called.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) Policy : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat()=0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat()=0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat()=0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat()=0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod()=0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod()=0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode()=0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode()=0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue()=0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue()=0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR,ERole)=0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility()=0;
};
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
  virtual HRESULT STDMETHODCALLTYPE ClearAll()=0; // never called
};
class HString {
 public:
  HSTRING value=nullptr;
  explicit HString(const std::wstring& text={}) {if(!text.empty())CheckHr(WindowsCreateString(text.c_str(),static_cast<UINT32>(text.size()),&value),"WindowsCreateString");}
  ~HString(){if(value)WindowsDeleteString(value);}
  std::wstring Text()const{UINT32 length=0;const auto p=WindowsGetStringRawBuffer(value,&length);return {p,length};}
};
ComPtr<AppPolicy> AppFactory(){
  HString name(L"Windows.Media.Internal.AudioPolicyConfig");ComPtr<AppPolicy> result;
  CheckHr(RoGetActivationFactory(name.value,__uuidof(AppPolicy),reinterpret_cast<void**>(result.GetAddressOf())),"per-app routing unavailable");return result;
}
std::wstring AppId(const std::wstring& id){return id.empty()?L"":L"\\\\?\\SWD#MMDEVAPI#"+id+L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";}
bool Same(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
bool ReadAppRoute(AppPolicy* policy,unsigned pid,int role,HString& value){
  const auto hr=policy->GetPersisted(pid,eRender,static_cast<ERole>(role),&value.value);
  // A browser process can outlive the audio identity used by this internal
  // API. E_INVALIDARG is an unresolved identity, not a valid empty preference.
  if(hr==E_INVALIDARG)return false;
  if(hr!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))CheckHr(hr,"read source-app route");
  return true;
}
std::wstring Default(ERole role){auto e=Enumerator();ComPtr<IMMDevice>d;CheckHr(e->GetDefaultAudioEndpoint(eRender,role,&d),"GetDefaultAudioEndpoint");wchar_t* p=nullptr;CheckHr(d->GetId(&p),"GetId");std::wstring id(p);CoTaskMemFree(p);return id;}
void SetDefault(const std::wstring& id,ERole role){
  CLSID cls;CheckHr(CLSIDFromString(L"{870af99c-171d-4f9e-af0d-e63df40c2bc9}",&cls),"PolicyConfig CLSID");ComPtr<Policy> p;
  CheckHr(CoCreateInstance(cls,nullptr,CLSCTX_ALL,__uuidof(Policy),reinterpret_cast<void**>(p.GetAddressOf())),"default routing unavailable");
  CheckHr(p->SetDefaultEndpoint(id.c_str(),role),"SetDefaultEndpoint");
}
SourceApp Process(unsigned pid){
  SourceApp app{pid,{},{},0};HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return app;
  DWORD exit_code=0;
  if(!GetExitCodeProcess(h,&exit_code) || exit_code!=STILL_ACTIVE){CloseHandle(h);return app;}
  wchar_t path[32768];DWORD size=32768;FILETIME created,exit,kernel,user;
  if(QueryFullProcessImageNameW(h,0,path,&size)){app.path.assign(path,size);app.name=std::filesystem::path(app.path).stem().wstring();}
  if(GetProcessTimes(h,&created,&exit,&kernel,&user))app.started=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
  CloseHandle(h);return app;
}
std::vector<SourceApp> LiveProcesses(const std::wstring& path){
  std::vector<SourceApp> result;
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot enumerate source-app processes for restoration");
  PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
  if(Process32FirstW(snapshot,&entry))do{
    if(entry.th32ProcessID==GetCurrentProcessId())continue;
    auto candidate=Process(entry.th32ProcessID);
    if(!candidate.path.empty() && Same(candidate.path,path))result.push_back(std::move(candidate));
  }while(Process32NextW(snapshot,&entry));
  CloseHandle(snapshot);return result;
}
}
struct AudioRouting::VolumeBridge {
  // Declared first so COM services/callbacks are released before RoUninitialize.
  RuntimeApartment apartment;
  struct Change { VolumeState value; bool physical=false; };
  struct Inbox { std::mutex mutex;std::optional<Change> latest;bool listening=true; };
  static constexpr GUID context={0xd4f281d3,0xc7e7,0x47e0,{0x8c,0xf3,0x8b,0x7d,0xd6,0x5d,0x12,0x57}};
  class Callback final:public IAudioEndpointVolumeCallback {
   public:
    explicit Callback(std::shared_ptr<Inbox> inbox,bool physical):inbox_(std::move(inbox)),physical_(physical){
      CheckHr(CoCreateFreeThreadedMarshaler(this,&marshaller_),"marshal volume notifications");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** object)override{
      if(!object)return E_POINTER;*object=nullptr;
      if(iid==__uuidof(IUnknown) || iid==__uuidof(IAudioEndpointVolumeCallback)){
        *object=static_cast<IAudioEndpointVolumeCallback*>(this);AddRef();return S_OK;
      }
      if(iid==__uuidof(IMarshal))return marshaller_->QueryInterface(iid,object);
      return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data)override{
      if(!data || IsEqualGUID(data->guidEventContext,context) || !std::isfinite(data->fMasterVolume))return S_OK;
      // Core Audio callbacks only coalesce state. COM/file/route work stays on
      // the controller; callbacks never block on or touch an audio worker.
      std::lock_guard lock(inbox_->mutex);
      if(inbox_->listening)inbox_->latest=Change{{data->fMasterVolume,data->bMuted!=FALSE},physical_};
      return S_OK;
    }
   private:
    std::atomic<ULONG> references_{1};std::shared_ptr<Inbox> inbox_;ComPtr<IUnknown> marshaller_;bool physical_;
  };
  ComPtr<IAudioEndpointVolume> cable,physical;
  ComPtr<Callback> cable_callback,physical_callback;
  std::shared_ptr<Inbox> inbox=std::make_shared<Inbox>();
  bool cable_registered=false,physical_registered=false;
  static ComPtr<IAudioEndpointVolume> Open(const std::wstring& id){
    auto device=Device(id);CheckFlow(device.Get(),eRender);ComPtr<IAudioEndpointVolume> result;
    CheckHr(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&result),"open master volume");return result;
  }
  VolumeBridge(const std::wstring& cable_id,const std::wstring& physical_id,bool restoring=false){
    if(!restoring){cable=Open(cable_id);physical=Open(physical_id);}
    else{
      // Restore the physical level even if the virtual driver vanished.
      try{cable=Open(cable_id);}catch(const std::exception&){}
      try{physical=Open(physical_id);}catch(const std::exception&){}
    }
  }
  ~VolumeBridge(){Unwatch();}
  static VolumeState Read(IAudioEndpointVolume* endpoint){
    if(!endpoint)throw std::runtime_error("volume endpoint is unavailable; reconnect it to finish restoration");
    VolumeState result;BOOL mute=FALSE;
    CheckHr(endpoint->GetMasterVolumeLevelScalar(&result.level),"read master volume");
    CheckHr(endpoint->GetMute(&mute),"read master mute");result.muted=mute!=FALSE;
    if(!std::isfinite(result.level) || result.level<0 || result.level>1)throw std::runtime_error("invalid master volume");
    return result;
  }
  static void Write(IAudioEndpointVolume* endpoint,VolumeState value){
    const auto current=Read(endpoint);
    if(value.muted && !current.muted)CheckHr(endpoint->SetMute(TRUE,&context),"set master mute");
    if(std::abs(current.level-value.level)>.00001f)
      CheckHr(endpoint->SetMasterVolumeLevelScalar(value.level,&context),"set master volume");
    if(!value.muted && current.muted)CheckHr(endpoint->SetMute(FALSE,&context),"clear master mute");
  }
  void UnityCable(){
    float minimum=0,maximum=0,step=0;
    CheckHr(cable->GetVolumeRange(&minimum,&maximum,&step),"read cable volume range");
    if(minimum>0)throw std::runtime_error("disable VB-CABLE Windows Volume Control before using this route");
    // Scalar100% can include endpoint boost. Unity means0dB, never max gain.
    CheckHr(cable->SetMasterVolumeLevel(std::min(0.0f,maximum),&context),"set cable transport to0dB");
    CheckHr(cable->SetMute(FALSE,&context),"unmute cable transport");
  }
  void Watch(bool cable_controls){
    physical_callback.Attach(new Callback(inbox,true));
    CheckHr(physical->RegisterControlChangeNotify(physical_callback.Get()),"watch physical volume");physical_registered=true;
    if(!cable_controls)return;
    cable_callback.Attach(new Callback(inbox,false));
    CheckHr(cable->RegisterControlChangeNotify(cable_callback.Get()),"watch cable volume");cable_registered=true;
  }
  void Unwatch(){
    {std::lock_guard lock(inbox->mutex);inbox->listening=false;}
    if(cable_registered)cable->UnregisterControlChangeNotify(cable_callback.Get());
    if(physical_registered)physical->UnregisterControlChangeNotify(physical_callback.Get());
    cable_registered=physical_registered=false;
  }
  std::optional<Change> Latest(){std::lock_guard lock(inbox->mutex);auto change=inbox->latest;inbox->latest.reset();return change;}
};
AudioRouting::AudioRouting(std::filesystem::path journal):journal_(std::move(journal)){}
AudioRouting::~AudioRouting()=default;
std::vector<SourceApp> LiveSourceProcesses(const std::wstring& path){return LiveProcesses(path);}
std::vector<SourceApp> SourceApps(){
  Apartment apartment;std::vector<SourceApp> result;std::set<unsigned> seen;
  for(const auto& e:EnumerateAudioEndpoints())if(e.flow==EndpointFlow::Render && e.state==1){
    try{
      auto d=Device(e.id);ComPtr<IAudioSessionManager2>manager;CheckHr(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager),"session manager");
      ComPtr<IAudioSessionEnumerator>sessions;CheckHr(manager->GetSessionEnumerator(&sessions),"session enumeration");int count=0;sessions->GetCount(&count);
      for(int i=0;i<count;++i){ComPtr<IAudioSessionControl>s;ComPtr<IAudioSessionControl2>s2;DWORD pid=0;
        if(FAILED(sessions->GetSession(i,&s)) || FAILED(s.As(&s2)) || FAILED(s2->GetProcessId(&pid)) || !pid || pid==GetCurrentProcessId())continue;
        AudioSessionState state;if(FAILED(s->GetState(&state)) || state==AudioSessionStateExpired)continue;
        if(!seen.insert(pid).second)continue;
        auto app=Process(pid);if(!app.path.empty())result.push_back(std::move(app));
      }
    }catch(const std::exception&){} // Unavailable session does not hide other apps.
  }
  return result;
}
void ReturnToPhysical(const std::wstring& cable,const std::wstring& physical){
  RuntimeApartment apartment;
  for(int role=0;role<2;++role)if(Same(Default(static_cast<ERole>(role)),cable))SetDefault(physical,static_cast<ERole>(role));
}
void SelectPhysicalOutput(const std::wstring& cable,const std::wstring& physical,unsigned pid){
  RuntimeApartment apartment;
  const auto endpoint=InspectAudioEndpoint(physical);
  if(Same(cable,physical) || endpoint.flow!=EndpointFlow::Render || endpoint.state!=DEVICE_STATE_ACTIVE || !endpoint.error.empty() || endpoint.name.find(L"VB-Audio")!=std::wstring::npos)
    throw std::runtime_error("selected playback device is unavailable");
  if(pid){
    if(Process(pid).path.empty())throw std::runtime_error("selected source app is no longer running");
    auto policy=AppFactory();
    // The chosen physical endpoint becomes the Windows default below. Let the
    // app follow it rather than leave a physical pin that bypasses All system.
    for(int role=0;role<2;++role)CheckHr(policy->SetPersisted(pid,eRender,static_cast<ERole>(role),nullptr),"return source app to default playback");
  }
  for(int role=0;role<2;++role)if(!Same(Default(static_cast<ERole>(role)),physical))SetDefault(physical,static_cast<ERole>(role));
}
void AudioRouting::Save(){
  std::filesystem::create_directories(journal_.parent_path());auto temp=journal_;temp+=L".tmp";
  {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot save audio-route restoration journal");
    out<<std::setprecision(9)<<"version=5\nall_system="<<all_system_<<"\npid="<<pid_<<"\nstarted="<<process_started_<<"\npath="<<Utf8(process_path_)<<"\ncable="<<Utf8(cable_)<<"\nphysical="<<Utf8(physical_)
       <<"\nconsole="<<Utf8(previous_[0])<<"\nmultimedia="<<Utf8(previous_[1])
       <<"\ndefaults_owned="<<defaults_owned_<<"\ndefault_console="<<Utf8(defaults_[0])<<"\ndefault_multimedia="<<Utf8(defaults_[1])
       <<"\nowner="<<Utf8(owner_)<<"\nvolume_owned="<<volume_owned_<<"\nphysical_volume_owned="<<physical_volume_owned_
       <<"\ncable_volume="<<cable_before_.level<<"\ncable_mute="<<cable_before_.muted
       <<"\nphysical_volume="<<physical_before_.level<<"\nphysical_mute="<<physical_before_.muted
       <<"\nmaster_volume="<<master_.level<<"\nmaster_mute="<<master_.muted<<'\n';out.flush();if(!out)throw std::runtime_error("audio-route journal write failed");}
  CheckHr(MoveFileExW(temp.c_str(),journal_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"save route journal");
}
void AudioRouting::Load(){
  if(owned_ || !std::filesystem::exists(journal_))return;
  std::ifstream in(journal_,std::ios::binary);std::map<std::string,std::string>v;std::string line;
  while(std::getline(in,line)){auto p=line.find('=');if(p!=std::string::npos)v[line.substr(0,p)]=line.substr(p+1);}
  if((v["version"]!="1" && v["version"]!="2" && v["version"]!="3" && v["version"]!="4" && v["version"]!="5") || v["cable"].empty())throw std::runtime_error("audio-route restoration journal is invalid");
  pid_=std::stoul(v.at("pid"));process_started_=std::stoull(v.at("started"));process_path_=Wide(v.at("path"));
  cable_=Wide(v.at("cable"));physical_=Wide(v.at("physical"));previous_[0]=Wide(v.at("console"));previous_[1]=Wide(v.at("multimedia"));owned_=true;
  all_system_=v["version"]=="5"?v["all_system"]=="1":pid_==0;
  defaults_owned_=v["version"]!="1" && v["defaults_owned"]=="1";
  defaults_[0]=Wide(v["default_console"]);defaults_[1]=Wide(v["default_multimedia"]);
  owner_=Wide(v["owner"]);volume_owned_=(v["version"]=="3" || v["version"]=="4" || v["version"]=="5") && v["volume_owned"]=="1";
  physical_volume_owned_=volume_owned_ && v["physical_volume_owned"]=="1";
  legacy_fullscale_=v["version"]=="3" && physical_volume_owned_;
  if(volume_owned_){
    auto read=[&](const char* level,const char* mute){VolumeState s{std::stof(v.at(level)),v.at(mute)=="1"};
      if(!std::isfinite(s.level) || s.level<0 || s.level>1)throw std::runtime_error("invalid restoration volume");return s;};
    cable_before_=read("cable_volume","cable_mute");physical_before_=read("physical_volume","physical_mute");master_=read("master_volume","master_mute");
  }
}
void AudioRouting::Begin(const std::wstring& cable,const std::wstring& physical,unsigned pid,const std::wstring& owner){
  Restore();RestorePending();RuntimeApartment apartment;cable_=cable;physical_=physical;pid_=pid;process_path_.clear();process_started_=0;defaults_owned_=false;
  all_system_=pid==0;
  // The last source can have a saved explicit physical route (including pins
  // made by older Sukut versions). Carry it into the global lease so that it
  // joins the cable too, with its exact old choices restored on Stop/crash.
  if(all_system_ && last_source_.pid){
    const auto app=Process(last_source_.pid);
    if(app.started==last_source_.started && Same(app.path,last_source_.path))pid_=app.pid;
    else for(const auto& active:SourceApps())if(Same(active.path,last_source_.path)){pid_=active.pid;break;}
  }
  owner_=owner;volume_=std::make_unique<VolumeBridge>(cable_,physical_);
  cable_before_=VolumeBridge::Read(volume_->cable.Get());physical_before_=VolumeBridge::Read(volume_->physical.Get());master_=physical_before_;
  volume_owned_=true;physical_volume_owned_=false;legacy_fullscale_=false;
  if(pid_){
    const auto app=Process(pid_);if(app.path.empty())throw std::runtime_error("selected source app is no longer running");process_path_=app.path;process_started_=app.started;
    if(!all_system_)last_source_=app;
    auto p=AppFactory();bool resolved=true;
    for(int r=0;r<2;++r){HString old;resolved=ReadAppRoute(p.Get(),pid_,r,old) && resolved;previous_[r]=old.Text();if(Same(previous_[r],AppId(cable_)))previous_[r]=L"";}
    if(!resolved){
      if(!all_system_)throw std::runtime_error("The source app has no available audio session. Play audio and choose the app again.");
      // Global audio can start now. CheckRoute will carry this source over
      // once its audio identity is available, with its old preference saved.
      pid_=0;process_path_.clear();process_started_=0;
    }else{
      for(int r=0;r<2;++r){defaults_[r]=Default(static_cast<ERole>(r));if(Same(defaults_[r],cable_))defaults_[r]=physical_;}
      defaults_owned_=true;
    }
  }
  if(!pid_)for(int r=0;r<2;++r){previous_[r]=Default(static_cast<ERole>(r));if(previous_[r]==cable_)previous_[r]=physical_;}
  Save();owned_=true; // Record prior routes before either setter, including partial failure.
  try{
    // The real device is always the master. Global routing requires the user's
    // confirmed driver passthrough option: cable percentages are metadata only.
    // Never force the physical endpoint to100% or amplify samples to undo gain.
    if(all_system_)VolumeBridge::Write(volume_->cable.Get(),master_);else volume_->UnityCable();
    if(pid_){
      // Specific-app mode keeps the physical default for device effects.
      // Global mode selects cable and carries the previous app along with it.
      const auto& target=all_system_?cable_:physical_;
      for(int r=0;r<2;++r)if(!Same(Default(static_cast<ERole>(r)),target))SetDefault(target,static_cast<ERole>(r));
      auto p=AppFactory();HString id(AppId(cable_));for(int r=0;r<2;++r)CheckHr(p->SetPersisted(pid_,eRender,static_cast<ERole>(r),id.value),"route source app to CABLE Input");
    }
    else for(int r=0;r<2;++r)SetDefault(cable_,static_cast<ERole>(r));
    volume_->Watch(all_system_);
  }catch(...){try{Restore();}catch(...){}throw;}
}
void AudioRouting::PollVolume(){
  if(!owned_ || !volume_owned_ || !volume_)return;
  if(const auto change=volume_->Latest()){
    master_=change->value;
    if(all_system_){
      if(change->physical)VolumeBridge::Write(volume_->cable.Get(),master_);
      else VolumeBridge::Write(volume_->physical.Get(),master_);
    }
    Save();
  }
}
void AudioRouting::CheckRoute(){
  if(!owned_)return;
  RuntimeApartment apartment;
  if(all_system_){
    for(int role=0;role<2;++role)if(!Same(Default(static_cast<ERole>(role)),cable_))
      throw std::runtime_error("Windows audio output changed outside Sukut. Restart audio to apply the selected route.");
    if(!pid_ && !last_source_.path.empty()){
      for(const auto& app:SourceApps())if(Same(app.path,last_source_.path)){
        auto policy=AppFactory();std::wstring prior[2];bool resolved=true;
        for(int role=0;role<2;++role){HString current;resolved=ReadAppRoute(policy.Get(),app.pid,role,current) && resolved;prior[role]=current.Text();}
        if(!resolved)continue;
        for(int role=0;role<2;++role){defaults_[role]=previous_[role];previous_[role]=Same(prior[role],AppId(cable_))?L"":prior[role];}
        pid_=app.pid;process_path_=app.path;process_started_=app.started;defaults_owned_=true;Save();
        HString id(AppId(cable_));
        for(int role=0;role<2;++role)CheckHr(policy->SetPersisted(pid_,eRender,static_cast<ERole>(role),id.value),"include previous source in All system audio");
        break;
      }
    }
  }else{
    const auto process=Process(pid_);
    if(process.started!=process_started_ || process.path!=process_path_)
      throw std::runtime_error("selected source app is no longer running");
    auto policy=AppFactory();
    for(int role=0;role<2;++role){HString current;
      if(!ReadAppRoute(policy.Get(),pid_,role,current))throw std::runtime_error("selected source app is no longer running");
      if(!Same(current.Text(),AppId(cable_)))throw std::runtime_error("The source app audio route changed. Restart audio to apply the selected route.");}
  }
}
void AudioRouting::RestorePending(unsigned active_pid,bool all_system_active){
  // A returning app's existing cable route already belongs in the active
  // global mix. Restoring its physical pin here would bypass that mix.
  if(all_system_active)return;
  const auto directory=journal_.parent_path()/L"pending-app-routes";
  std::error_code error;
  if(!std::filesystem::is_directory(directory,error))return;
  const auto active=active_pid?Process(active_pid):SourceApp{};
  // A closed app's persisted route requires a live PID. Keep its exact prior
  // choices on disk, and retry outside the audio workers as soon as it reopens,
  // including before it has created an audio session on the stale cable route.
  for(const auto& entry:std::filesystem::directory_iterator(directory,error)){
    if(entry.path().extension()!=L".ini")continue;
    try{
      AudioRouting pending(entry.path());pending.Load();
      // Never undo the route of the app currently being filtered.
      if(!active.path.empty() && Same(active.path,pending.process_path_))continue;
      pending.Restore({},false);
    }catch(const std::exception&){/* Retain the record and retry on a later poll. */}
  }
}
void AudioRouting::Restore(const std::wstring& expected_owner,bool defer_missing_source){
  RestoreLock restore_lock;Load();if(!owned_ || (!expected_owner.empty() && expected_owner!=owner_))return;RuntimeApartment apartment;
  bool missing_source=false;
  bool source_rebound=false;
  std::vector<SourceApp> restore_targets;
  if(pid_){
    const auto app=Process(pid_);
    if(app.started!=process_started_ || !Same(app.path,process_path_)){
      source_rebound=true;
      restore_targets=LiveProcesses(process_path_);
      missing_source=restore_targets.empty();
      if(missing_source && !defer_missing_source)return;
    }else restore_targets.push_back(app);
  }
  if(volume_owned_){
    if(!volume_)volume_=std::make_unique<VolumeBridge>(cable_,physical_,true);
    if(!legacy_fullscale_)PollVolume(); // finish the last queued volume gesture
    volume_->Unwatch();
    if(physical_volume_owned_){
      const auto actual=VolumeBridge::Read(volume_->physical.Get());
      // A physical-key change received just before Stop/crash belongs to the
      // user. Otherwise transfer the current virtual master back before direct
      // playback can reach a temporarily full-scale physical endpoint.
      auto target=actual;
      if(actual.level>=.99999f && !actual.muted){
        target=physical_before_;
      }
      VolumeBridge::Write(volume_->physical.Get(),target);physical_volume_owned_=false;Save();
    }
  }
  if(pid_ && !missing_source){
    auto p=AppFactory();
    const auto sessions=SourceApps();
    // Also try the browser's other live processes: its original PID may still
    // exist after Windows has stopped resolving that PID's audio identity.
    for(const auto& candidate:LiveProcesses(process_path_))
      if(std::none_of(restore_targets.begin(),restore_targets.end(),[&](const auto& target){return target.pid==candidate.pid;}))restore_targets.push_back(candidate);
    bool role_ready[2]={false,false};
    for(const auto& target:restore_targets)for(int r=0;r<2;++r){HString current;
      if(!ReadAppRoute(p.Get(),target.pid,r,current))continue;
      if(Same(current.Text(),AppId(cable_))){
        HString prior(previous_[r]);const auto hr=p->SetPersisted(target.pid,eRender,static_cast<ERole>(r),prior.value);
        if(hr==E_INVALIDARG || hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND))continue;
        CheckHr(hr,"restore source-app audio route");
        HString restored;if(!ReadAppRoute(p.Get(),target.pid,r,restored))continue;
        if(!Same(restored.Text(),previous_[r]))throw std::runtime_error("source-app audio route restoration is pending");
        role_ready[r]=true;
      }
      else if(!current.Text().empty() || (!source_rebound && target.pid==pid_) || std::any_of(sessions.begin(),sessions.end(),[&](const auto& session){return session.pid==target.pid;}))role_ready[r]=true;
    }
    if(!role_ready[0] || !role_ready[1]){
      // A new PID may exist before Windows can resolve its per-app audio
      // identity. An empty result here must not consume the saved old route.
      if(!defer_missing_source)return;
      missing_source=true;
    }
  }else if(!pid_)for(int r=0;r<2;++r)if(Same(Default(static_cast<ERole>(r)),cable_))SetDefault(previous_[r],static_cast<ERole>(r));
  if(defaults_owned_)for(int r=0;r<2;++r)if(Same(Default(static_cast<ERole>(r)),all_system_?cable_:physical_))SetDefault(defaults_[r],static_cast<ERole>(r));
  defaults_owned_=false;
  if(volume_owned_){VolumeBridge::Write(volume_->cable.Get(),cable_before_);volume_owned_=false;Save();}
  if(missing_source){
    // Global defaults and volume have already been restored. Only the absent
    // app's persisted choices remain; they must not lock the whole bridge.
    Save();
    const auto directory=journal_.parent_path()/L"pending-app-routes";
    std::filesystem::create_directories(directory);
    GUID id;CheckHr(CoCreateGuid(&id),"create pending route identity");
    wchar_t name[40];StringFromGUID2(id,name,40);
    const auto pending=directory/(std::wstring(name)+L".ini");
    CheckHr(MoveFileExW(journal_.c_str(),pending.c_str(),MOVEFILE_WRITE_THROUGH)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"defer closed source-app route");
  }else if(!DeleteFileW(journal_.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("cannot clear restored audio-route journal");
  owned_=false;defaults_owned_=false;physical_volume_owned_=false;volume_.reset();
}
}
