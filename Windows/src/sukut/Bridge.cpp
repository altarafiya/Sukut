#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include "desktop/DesktopController.h"
#include "sukut/AudioRouting.h"
#include "sukut/VolumeGuard.h"

using namespace cleanwave;
namespace {
thread_local std::string last_error;
struct BufferingSettings {
  std::atomic<int> requested{0}, applied{0};
  std::atomic<bool> cable_passthrough_confirmed{false};
  std::atomic<std::uint64_t> session{0};
  std::shared_ptr<std::atomic<int>> amplification_db=std::make_shared<std::atomic<int>>(0);
};
class RoutedBackend final:public DesktopBackend {
 public:
  RoutedBackend(const std::filesystem::path& root,const DesktopPreferences& p,std::shared_ptr<std::atomic<unsigned>> source,
                std::shared_ptr<std::atomic<int>> preset_mode, std::shared_ptr<BufferingSettings> buffering)
    :root_(root),preferences_(p),routing_(root/L"runtime/sukut-routing.ini"),source_(std::move(source)),preset_mode_(std::move(preset_mode)),buffering_(std::move(buffering)) {
    routing_.Restore();routing_.RestorePending();
  }
  ~RoutedBackend(){Stop();if(lease_)CloseHandle(lease_);}
  void Configure(const DesktopPreferences& p)override{
    preferences_=p;
    if(session_)ApplyConfiguration();
  }
  void SelectRoute(const AudioRoute& route)override{
    if(session_)session_->SelectRoute(route);
    else ValidateAudioRoute(route,EnumerateAudioEndpoints());
    preferences_.route=route;
  }
  void Start()override{
    // The driver volume option is an advisory setup check, not a user checkbox
    // or a measured capability. Keep the legacy ABI setter for old callers.
    if(lease_)throw std::runtime_error("bridge is already active");
    lease_=CreateMutexW(nullptr,FALSE,L"Local\\CleanWaveWindowsDesktop");
    if(!lease_ || GetLastError()==ERROR_ALREADY_EXISTS){if(lease_)CloseHandle(lease_);lease_=nullptr;throw std::runtime_error("close the CleanWave preview before starting Sukut");}
    try{
      // The interpreter, tensor arena, FFT buffers and audio pipeline only
      // exist while processing. OFF never loads the model at startup.
      const int level=buffering_->requested.load();
      session_=std::make_unique<LiveSession>(root_/L"assets/engine/separator.tflite",root_/L"assets/engine/window_576_q15.bin",root_/L"assets/engine/window_960_q15.bin",preferences_.route,true,level,false,buffering_->amplification_db,true);
      ApplyConfiguration();session_->Start();
      volume_guard_=std::make_unique<sukut::VolumeGuard>(root_);
      routing_.Begin(preferences_.route.source_render_id,preferences_.route.physical_render_id,source_->load(),volume_guard_->Owner());route_error_.clear();
      volume_guard_->Check();
      buffering_->applied.store(level);++buffering_->session;
    }catch(...){Stop();throw;}
  }
  bool Stop()override{
    // Never release buffers while audio workers still hold them.
    if(session_ && !session_->Stop())return false;
    session_.reset();
    try{
      routing_.Restore();
      if(volume_guard_){volume_guard_->Stop();volume_guard_.reset();}
      route_error_.clear();
    }
    catch(const std::exception& e){route_error_=e.what();}
    // Keep the lease/guard when restoration fails, allowing OFF to retry and
    // preventing another active bridge from replacing this outstanding journal.
    if(route_error_.empty() && lease_){CloseHandle(lease_);lease_=nullptr;}return route_error_.empty();
  }
  LiveSnapshot Snapshot()override{
    auto s=session_?session_->Snapshot(false):LiveSnapshot{};
    if(session_){
      try{if(volume_guard_)volume_guard_->Check();routing_.PollVolume();}
      catch(const std::exception& e){s.state=LiveState::Faulted;s.error=e.what();}
    }
    if(!session_){s.controls.mode=preferences_.mode;s.controls.custom=preferences_.custom;s.controls.effective=preferences_.custom;}
    if(!route_error_.empty()){s.state=LiveState::Faulted;s.error=route_error_;}
    const auto now=std::chrono::steady_clock::now();
    if(now>=next_route_check_){
      next_route_check_=now+std::chrono::seconds(1);
      const bool active=session_ && s.state==LiveState::Running;
      try{routing_.RestorePending(active?source_->load():0,active && source_->load()==0);}catch(const std::exception&){/* Keep deferred app records without stopping unrelated audio. */}
      if(session_ && s.state==LiveState::Running)
        try{routing_.CheckRoute();}catch(const std::exception& e){s.state=LiveState::Faulted;s.error=e.what();}
    }
    try{
      if(s.state==LiveState::Faulted && s.error!=last_logged_error_){WriteLiveStatus(root_/L"runtime/sukut-last-fault.json",s);last_logged_error_=s.error;}
      if(s.state==LiveState::Running && now>=next_health_log_){WriteLiveStatus(root_/L"runtime/sukut-health.json",s);next_health_log_=now+std::chrono::seconds(5);}
    }catch(...){/* Diagnostic storage must never stop audio. */}
    return s;
  }
 private:
  void ApplyConfiguration(){
    const int mode=preset_mode_->load();
    if(preferences_.mode==ProcessingMode::Custom)session_->Configure(preferences_.custom,mode!=0);
    else session_->SetMode(preferences_.mode);
  }
  std::filesystem::path root_;DesktopPreferences preferences_;
  std::unique_ptr<LiveSession> session_;
  sukut::AudioRouting routing_;
  std::unique_ptr<sukut::VolumeGuard> volume_guard_;
  std::shared_ptr<std::atomic<unsigned>>source_;std::shared_ptr<std::atomic<int>>preset_mode_;
  std::shared_ptr<BufferingSettings>buffering_;
  HANDLE lease_=nullptr;std::string route_error_;
  std::chrono::steady_clock::time_point next_route_check_{},next_health_log_{};
  std::string last_logged_error_;
};
struct Context {
  std::unique_ptr<DesktopController>controller;
  std::shared_ptr<std::atomic<unsigned>>source=std::make_shared<std::atomic<unsigned>>(0);
  std::shared_ptr<std::atomic<int>>preset_mode=std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<BufferingSettings>buffering=std::make_shared<BufferingSettings>();
};
int PresetGain(float db){
  if(!std::isfinite(db) || db < -100.0f || db > 10.0f)throw std::runtime_error("invalid Sukut stem gain");
  return static_cast<int>(std::lround(db));
}
int Copy(const std::string& text,char* output,int capacity){const auto required=static_cast<int>(text.size()+1);if(output && capacity>=required)std::memcpy(output,text.c_str(),required);return required;}
std::string Quote(const std::wstring& value){return "\""+EscapeJson(Utf8(value))+"\"";}
}
#define EXPORT extern "C" __declspec(dllexport)
EXPORT int __cdecl Sukut_RestoreRouting(const wchar_t* root,const wchar_t* owner){
  try{sukut::AudioRouting routing(std::filesystem::path(root)/L"runtime/sukut-routing.ini");routing.Restore(owner);return 1;}
  catch(const std::exception& e){last_error=e.what();return 0;}
}
EXPORT int __cdecl Sukut_SetCablePassthroughConfirmed(void* handle,int confirmed){
  static_cast<Context*>(handle)->buffering->cable_passthrough_confirmed.store(confirmed!=0);return 1;
}
EXPORT void* __cdecl Sukut_Create(const wchar_t* root){
  try{auto c=std::make_unique<Context>();auto r=std::filesystem::path(root);auto source=c->source;auto preset_mode=c->preset_mode;auto buffering=c->buffering;
    const auto route_file=r/L"docs/route.json";
    const auto fallback=std::filesystem::exists(route_file)?ReadPreparedRoute(route_file):AudioRoute{};
    c->controller=std::make_unique<DesktopController>(r/L"runtime/sukut-engine.ini",fallback,
      [r,source,preset_mode,buffering](const auto& p){return std::make_unique<RoutedBackend>(r,p,source,preset_mode,buffering);},[](bool){});
    return c.release();
  }catch(const std::exception& e){last_error=e.what();return nullptr;}
}
EXPORT int __cdecl Sukut_Error(char* output,int capacity){return Copy(last_error,output,capacity);}
static int ReadSnapshot(void* handle,std::int64_t known_endpoints,char* output,int capacity){
  try{auto& c=*static_cast<Context*>(handle);auto s=c.controller->Snapshot();std::ostringstream o;
    o<<"{\"initialized\":"<<s.initialized<<",\"busy\":"<<s.busy<<",\"state\":"<<static_cast<int>(s.live.state)<<",\"paused\":"<<s.live.cleaner.paused
     <<",\"session\":"<<c.buffering->session.load()<<",\"fallbacks\":"<<s.live.cleaner.fallbacks<<",\"recoveries\":"<<s.live.cleaner.recoveries
     <<",\"bypasses\":"<<s.live.cleaner.bypasses<<",\"coveredOverruns\":"<<s.live.cleaner.burst_covered_overruns
     <<",\"pipelineActive\":"<<s.live.pipeline.active
     <<",\"controllerAgeMs\":"<<(s.published.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-s.published).count():0)
     <<",\"processingMeanMs\":"<<s.live.pipeline.ai_engine.mean_ms<<",\"processingMaxMs\":"<<s.live.pipeline.ai_engine.max_ms
     <<",\"error\":\""<<EscapeJson(s.error.empty()?s.live.error:s.error)<<"\",\"output\":"<<Quote(s.preferences.route.physical_render_id)
     <<",\"source\":"<<Quote(s.preferences.route.source_render_id)<<",\"capture\":"<<Quote(s.preferences.route.capture_id)<<",\"endpointsVersion\":"<<s.endpoints_version;
    // The controller's revision changes on every device refresh. Polls retain
    // the previous list until then, avoiding repeated JSON strings/UI objects.
    // A negative revision requests a full reply (legacy ABI and initial read).
    if(known_endpoints<0 || static_cast<std::uint64_t>(known_endpoints)!=s.endpoints_version){
      o<<",\"endpoints\":[";
      bool first=true;for(const auto& e:s.endpoints)if(e.state==1 && e.error.empty()){
        if(!first)o<<',';first=false;o<<"{\"id\":"<<Quote(e.id)<<",\"name\":"<<Quote(e.name)<<",\"flow\":"<<(e.flow==EndpointFlow::Render?1:0)<<",\"virtual\":"
          <<(e.name.find(L"VB-Audio")!=std::wstring::npos)<<'}';}
      o<<']';
    }
    o<<'}';return Copy(o.str(),output,capacity);
  }catch(const std::exception& e){last_error=e.what();return -1;}
}
EXPORT int __cdecl Sukut_Read(void* handle,char* output,int capacity){return ReadSnapshot(handle,-1,output,capacity);}
EXPORT int __cdecl Sukut_ReadSince(void* handle,std::int64_t known_endpoints,char* output,int capacity){return ReadSnapshot(handle,known_endpoints,output,capacity);}
EXPORT int __cdecl Sukut_Apps(char* output,int capacity){
  try{std::ostringstream o;o<<'[';bool first=true;for(const auto& a:sukut::SourceApps()){if(!first)o<<',';first=false;o<<"{\"pid\":"<<a.pid<<",\"name\":"<<Quote(a.name)<<'}';}o<<']';return Copy(o.str(),output,capacity);}
  catch(const std::exception& e){last_error=e.what();return -1;}
}
EXPORT int __cdecl Sukut_SetBuffering(void* handle,int level){
  try{AudioBuffering::ForLevel(level);static_cast<Context*>(handle)->buffering->requested.store(level);return 1;}
  catch(const std::exception& e){last_error=e.what();return 0;}
}
EXPORT int __cdecl Sukut_SetAmplification(void* handle,int db){
  try{if(db<0 || db>12)throw std::runtime_error("invalid amplification gain");static_cast<Context*>(handle)->buffering->amplification_db->store(db,std::memory_order_relaxed);return 1;}
  catch(const std::exception& e){last_error=e.what();return 0;}
}
EXPORT int __cdecl Sukut_SelectPreset(void* handle,int mode,const wchar_t* source,const wchar_t* capture,const wchar_t* output,unsigned pid,
                                     float voice_db,float music_db,float background_db){
  try{auto& c=*static_cast<Context*>(handle);if(mode<0 || mode>3)throw std::runtime_error("invalid Sukut mode");
    const auto voice=PresetGain(voice_db),music=PresetGain(music_db),background=PresetGain(background_db);
    auto s=c.controller->Snapshot();if(!s.initialized)throw std::runtime_error(s.error.empty()?"audio engine is preparing":s.error);
    // A fresh package can be stopped/exited before VB-CABLE has been installed.
    // Restore the owned lease first; OFF must not validate/open an absent route.
    const bool route_change=mode!=0 && (s.live.state!=LiveState::Running || s.preferences.route.source_render_id!=source || s.preferences.route.capture_id!=capture || s.preferences.route.physical_render_id!=output || c.buffering->requested.load()!=c.buffering->applied.load());
    if(mode==0 || (s.live.state==LiveState::Running && (route_change || c.source->load()!=pid))){
      const auto stop=c.controller->Stop();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
      do{std::this_thread::sleep_for(std::chrono::milliseconds(20));s=c.controller->Snapshot();}while((s.completed_stop<stop || s.busy) && std::chrono::steady_clock::now()<deadline);
      if(s.completed_stop<stop || s.busy || s.live.state==LiveState::Running)throw std::runtime_error("bridge is still stopping");
      if(!s.error.empty())throw std::runtime_error(s.error);
    }
    c.source->store(pid);auto p=s.preferences;p.route={source,capture,output};
    // After releasing our routing lease, explicit OFF follows the user's
    // selected playback device, even if the prior default was different.
    // Crash/startup restoration retains the original journal behavior.
    if(mode==0){
      if(s.live.state==LiveState::Faulted && !s.live.error.empty())throw std::runtime_error(s.live.error);
      if(output[0]!=L'\0')sukut::SelectPhysicalOutput(source,output,pid);
    }
    // Active presets use the supplied mix; Default has a different legacy table.
    // Both music-muting modes keep silent recovery. Publish mix + policy together.
    c.preset_mode->store(mode);
    p.mode=mode==0?ProcessingMode::Off:ProcessingMode::Custom;
    if(mode!=0)p.custom={voice,music,background,100,0};
    const auto version=c.controller->Configure(p,route_change);
    if(mode && s.live.state!=LiveState::Running)c.controller->Start();
    // Serialize user commands through completed configuration/start work on
    // the dedicated native thread; the WinUI dispatcher remains responsive.
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    do{
      std::this_thread::sleep_for(std::chrono::milliseconds(20));const auto current=c.controller->Snapshot();
      if(current.applied_version>=version && !current.busy){
        if(!current.error.empty())throw std::runtime_error(current.error);
        if((mode==0 && current.live.state==LiveState::Stopped) || (mode!=0 && current.live.state==LiveState::Running))return 1;
        if(current.live.state==LiveState::Faulted)throw std::runtime_error(current.live.error);
      }
    }while(std::chrono::steady_clock::now()<until);
    c.controller->Stop();throw std::runtime_error("audio route is still switching");
  }catch(const std::exception& e){last_error=e.what();return 0;}
}
EXPORT int __cdecl Sukut_Select(void* handle,int mode,const wchar_t* source,const wchar_t* capture,const wchar_t* output,unsigned pid){
  return Sukut_SelectPreset(handle,mode,source,capture,output,pid,
    0.0f,mode!=0?-100.0f:0.0f,mode==3?-100.0f:0.0f);
}
EXPORT void __cdecl Sukut_Refresh(void* handle){static_cast<Context*>(handle)->controller->Refresh();}
EXPORT void __cdecl Sukut_Destroy(void* handle){auto* c=static_cast<Context*>(handle);if(c){c->controller->Shutdown();c->controller->Join();delete c;}}
