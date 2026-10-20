#include <chrono>
#include <algorithm>
#include <utility>
#include <stdexcept>
#include "desktop/DesktopController.h"

namespace cleanwave {
using namespace std::chrono_literals;
NativeDesktopBackend::NativeDesktopBackend(const std::filesystem::path& root, const DesktopPreferences& p,
    std::filesystem::path diagnostic_path)
    :session_(root/L"assets/engine/separator.tflite",root/L"assets/engine/window_576_q15.bin",
              root/L"assets/engine/window_960_q15.bin",p.route),diagnostic_path_(std::move(diagnostic_path)) {
  session_.Configure(p.custom);session_.SetMode(p.mode);
}
LiveSnapshot NativeDesktopBackend::Snapshot() {
  auto snapshot=session_.Snapshot();
  const auto now=std::chrono::steady_clock::now();
  if(!diagnostic_path_.empty() && (now-last_diagnostic_>=1s || snapshot.state!=last_state_ || snapshot.generation!=last_generation_)) {
    // Metadata only. File I/O runs on the controller, outside every audio worker.
    try { std::filesystem::create_directories(diagnostic_path_.parent_path());WriteLiveStatus(diagnostic_path_,snapshot); }
    catch(const std::exception&) {} // optional telemetry cannot stop working audio
    last_diagnostic_=now;last_state_=snapshot.state;last_generation_=snapshot.generation;
  }
  return snapshot;
}
void NativeDesktopBackend::Configure(const DesktopPreferences& p) {
  // Publish one active configuration per change; avoid a transient Custom hop
  // when the user selects Default/Off. Personal storage belongs to the presenter.
  if(p.mode==ProcessingMode::Custom)session_.Configure(p.custom);
  else session_.SetMode(p.mode);
}
DesktopController::DesktopController(std::filesystem::path settings, AudioRoute fallback, Factory factory,
    std::function<void(bool)> notify, EndpointReader reader)
    :settings_(std::move(settings)),fallback_(std::move(fallback)),factory_(std::move(factory)),
     notify_(std::move(notify)),read_endpoints_(std::move(reader)) {
  worker_=std::thread([this]{Run();}); // start only after every member exists
}
DesktopController::~DesktopController(){Shutdown();Join();}
void DesktopController::Join(){if(worker_.joinable())worker_.join();}
DesktopSnapshot DesktopController::Snapshot() const {std::lock_guard lock(mutex_);return snapshot_;}
std::uint64_t DesktopController::Configure(DesktopPreferences p,bool route_change){
  std::lock_guard lock(mutex_);const auto version=++version_;
  route_change=route_change || (update_ && update_->route_change);
  update_=Update{std::move(p),route_change,version};wake_.notify_one();return version;
}
void DesktopController::Start(){std::lock_guard lock(mutex_);if(!shutdown_)start_=true;wake_.notify_one();}
std::uint64_t DesktopController::Stop(){std::lock_guard lock(mutex_);const auto version=++stop_version_;stop_=true;start_=false;wake_.notify_one();return version;}
void DesktopController::Refresh(){std::lock_guard lock(mutex_);refresh_=true;wake_.notify_one();}
void DesktopController::Shutdown(){std::lock_guard lock(mutex_);shutdown_=true;start_=false;wake_.notify_one();}
void DesktopController::Publish(){
  working_.published=std::chrono::steady_clock::now();
  {std::lock_guard lock(mutex_);snapshot_=working_;}
  if(notify_)notify_(working_.closed);
}
void DesktopController::UpdateLive(){
  if(!backend_)return;
  working_.live=backend_->Snapshot();
  if(working_.live.state==LiveState::Faulted){
    if(working_.error.empty())working_.error=working_.live.error;
    backend_->Stop();working_.live=backend_->Snapshot();working_.status=L"Audio stopped — attention needed";
  }else if(working_.live.state==LiveState::Running){
    if(working_.live.pipeline.processed==0)working_.status=L"Bridge started — waiting for audio frames";
    else if(working_.live.cleaner.paused)working_.status=L"Original audio — processing recovery";
    else if(working_.live.cleaner.applied_mode==ProcessingMode::Off)working_.status=L"Original audio — Off";
    else working_.status=L"Cleaning audio — "+Wide(ModeName(working_.live.cleaner.applied_mode));
  }else{
    working_.status=L"Bridge stopped";
    const auto found=std::find_if(working_.endpoints.begin(),working_.endpoints.end(),[&](const auto& e){return e.id==working_.preferences.route.physical_render_id;});
    if(found==working_.endpoints.end() || found->state!=1)working_.status=L"Selected output unavailable";
  }
}
void DesktopController::Run(){
  try{
    try{working_.preferences=LoadDesktopPreferences(settings_,fallback_);}
    catch(const std::exception& e){working_.preferences={ProcessingMode::Off,{},fallback_};working_.persistence_warning=e.what();}
    Publish();
    working_.endpoints=read_endpoints_();
    ++working_.endpoints_version;
    backend_=factory_(working_.preferences);
    working_.initialized=true;working_.busy=false;UpdateLive();Publish();
  }catch(const std::exception& e){working_.busy=false;working_.error=e.what();working_.status=L"CleanWave could not initialize";Publish();}
  for(;;){
    std::optional<Update> update;bool start,stop,refresh,shutdown;std::uint64_t stop_version=0;
    {
      std::unique_lock lock(mutex_);wake_.wait_for(lock,200ms,[&]{return update_ || start_ || stop_ || refresh_ || shutdown_;});
      update=std::move(update_);update_.reset();start=std::exchange(start_,false);stop=std::exchange(stop_,false);
      refresh=std::exchange(refresh_,false);shutdown=shutdown_;
      stop_version=stop_version_;
    }
    if(shutdown){
      working_.busy=true;working_.status=L"Stopping audio before closing…";Publish();
      if(backend_){
        backend_->Stop();
        // A final slider/mode update must survive an immediate Close click.
        if(update){
          try{
            if(update->route_change)backend_->SelectRoute(update->preferences.route);
            backend_->Configure(update->preferences);working_.preferences=update->preferences;
            SaveDesktopPreferences(settings_,working_.preferences);working_.persistence_warning.clear();
          }catch(const std::exception& e){working_.persistence_warning=e.what();}
          working_.applied_version=update->version;
        }
        working_.live=backend_->Snapshot();backend_.reset();
      }
      working_.busy=false;working_.closed=true;working_.status=L"Closed";Publish();return;
    }
    if(update){
      try{
        if(!backend_)throw std::runtime_error("cleaner is not initialized");
        if(update->route_change)backend_->SelectRoute(update->preferences.route);
        backend_->Configure(update->preferences);
        working_.preferences=update->preferences;
        if(update->route_change)working_.error.clear();
        try{SaveDesktopPreferences(settings_,working_.preferences);working_.persistence_warning.clear();}
        catch(const std::exception& e){working_.persistence_warning=e.what();}
      }catch(const std::exception& e){working_.error=e.what();}
      working_.applied_version=update->version;
    }
    if(refresh){try{working_.endpoints=read_endpoints_();++working_.endpoints_version;}catch(const std::exception& e){working_.error=e.what();}}
    if(stop && backend_){
      working_.busy=true;working_.status=L"Stopping audio…";Publish();
      const bool stopped=backend_->Stop();working_.live=backend_->Snapshot();
      if(stopped && working_.live.state==LiveState::Stopped)working_.error.clear();
      else working_.error=working_.live.error.empty()?"audio restoration is incomplete":working_.live.error;
      working_.completed_stop=stop_version;working_.busy=false;
    }
    if(start && backend_){
      working_.busy=true;working_.status=L"Starting audio…";working_.error.clear();Publish();
      try{backend_->Start();}catch(const std::exception& e){working_.error=e.what();}
      working_.busy=false;
    }
    UpdateLive();Publish();
  }
}
} // namespace cleanwave
