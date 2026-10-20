#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>
#include "desktop/DesktopController.h"

using namespace cleanwave;
using namespace std::chrono_literals;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void MustFail(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
bool Wait(const std::function<bool()>& predicate){
  const auto deadline=std::chrono::steady_clock::now()+3s;
  do{if(predicate())return true;std::this_thread::sleep_for(2ms);}while(std::chrono::steady_clock::now()<deadline);return predicate();
}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
struct Counters{std::atomic<unsigned> starts{0},stops{0},configures{0};std::atomic<bool> worker_owned{true};};
class FakeBackend final:public DesktopBackend{
 public:
  FakeBackend(std::shared_ptr<Counters> counters,DesktopPreferences p):counts(std::move(counters)){Configure(p);}
  void Configure(const DesktopPreferences& p) override{
    ++counts->configures;snapshot.controls={p.mode,p.mode==ProcessingMode::Custom?p.custom:RealtimeMix{},p.custom,1};
    snapshot.cleaner.applied_mode=p.mode;
  }
  void SelectRoute(const AudioRoute& route) override{
    if(snapshot.state==LiveState::Running)throw std::runtime_error("stop before selecting output");
    if(route.physical_render_id==L"missing")throw std::runtime_error("selected output unavailable");
  }
  void Start() override{++counts->starts;std::this_thread::sleep_for(350ms);snapshot.state=LiveState::Running;snapshot.error.clear();}
  bool Stop() override{++counts->stops;std::this_thread::sleep_for(120ms);snapshot.state=LiveState::Stopped;return true;}
  LiveSnapshot Snapshot() override{return snapshot;}
  std::shared_ptr<Counters> counts;LiveSnapshot snapshot;
};
int Run(const std::filesystem::path& evidence){
  std::filesystem::create_directories(evidence);std::ostringstream report;
  report<<"Step 8 desktop controller/persistence verification (fake hardware backend; no live audio claim)\n";
  try{
    const AudioRoute route{L"source",L"capture",L"output-α"};
    const auto file=evidence/L"settings.ini";
    DesktopPreferences p{ProcessingMode::Custom,{-3,-16,-19,80,2},route};SaveDesktopPreferences(file,p);
    auto restored=LoadDesktopPreferences(file,{});
    Check(restored.mode==ProcessingMode::Custom && restored.custom.voice_db==-3 && restored.custom.music_db==-16 &&
      restored.custom.other_db==-19 && restored.custom.reduction_percent==80 && restored.custom.boost_db==2 && restored.route.physical_render_id==route.physical_render_id,
      "saved Custom/mode/Unicode endpoints did not roundtrip");
    const auto bom=evidence/L"bom.ini";std::ofstream(bom,std::ios::binary)<<"\xEF\xBB\xBF"<<Read(file);
    Check(LoadDesktopPreferences(bom,{}).custom.voice_db==-3,"UTF-8 BOM preferences failed");
    const auto original=Read(file);auto invalid=p;invalid.custom.voice_db=99;
    MustFail([&]{SaveDesktopPreferences(file,invalid);},"invalid slider limits were saved");
    Check(Read(file)==original,"rejected preferences changed the original file");
    const auto extended_file=evidence/L"extended-range.ini";
    for(const auto mode:{ProcessingMode::Custom,ProcessingMode::Default,ProcessingMode::Off}){
      for(const RealtimeMix mix:{RealtimeMix{-100,10,-100,100,0},RealtimeMix{10,-100,10,100,0}}){
        auto extended=p;extended.mode=mode;extended.custom=mix;SaveDesktopPreferences(extended_file,extended);
        const auto loaded=LoadDesktopPreferences(extended_file,{});
        Check(loaded.mode==mode && loaded.custom.voice_db==mix.voice_db && loaded.custom.music_db==mix.music_db &&
          loaded.custom.other_db==mix.other_db,"extended source endpoints/mode did not persist");
      }
    }
    const auto extended_original=Read(extended_file);
    for(const auto field:{&RealtimeMix::voice_db,&RealtimeMix::music_db,&RealtimeMix::other_db}){
      for(const int value:{-101,11}){
        auto rejected=p;rejected.custom.*field=value;
        MustFail([&]{SaveDesktopPreferences(extended_file,rejected);},"out-of-range source preference saved");
        Check(Read(extended_file)==extended_original,"rejected extended source preference changed saved bytes");
      }
    }
    const auto outside=evidence/L"out-of-range.ini";
    for(const auto key:{"voice_db","music_db","background_db"}){
      for(const int value:{-101,11}){
        auto text=original;const auto begin=text.find(std::string(key)+"=");const auto end=text.find('\n',begin);
        text.replace(begin,end-begin,std::string(key)+"="+std::to_string(value));
        {std::ofstream out(outside,std::ios::binary);out<<text;}
        MustFail([&]{LoadDesktopPreferences(outside,route);},"out-of-range source preference loaded");
      }
    }
    report<<"extended_source_range=PASS -100_to_10dB all_three_endpoints_roundtrip_in_Custom_Default_Off outside_range_load_save_rejected old_bytes_preserved\n";
    const auto malformed=evidence/L"malformed.ini";std::ofstream(malformed,std::ios::binary)<<"version=1\nmode=1\nmode=2\n";
    MustFail([&]{LoadDesktopPreferences(malformed,route);},"duplicate malformed settings accepted");
    Check(Read(malformed)=="version=1\nmode=1\nmode=2\n","malformed settings were overwritten");
    report<<"persistence=PASS Custom/mode/Unicode_endpoint_roundtrip atomic_replacement invalid_limits_and_duplicates_rejected old_bytes_preserved\n";
    auto counts=std::make_shared<Counters>();const auto main_thread=std::this_thread::get_id();
    std::vector<EndpointInfo> endpoints(1);endpoints[0].id=route.physical_render_id;endpoints[0].name=L"Test output";endpoints[0].state=1;
    DesktopController controller(file,route,[counts,main_thread](const auto& prefs){
      if(std::this_thread::get_id()==main_thread)counts->worker_owned=false;
      std::this_thread::sleep_for(150ms);return std::make_unique<FakeBackend>(counts,prefs);
    },[](bool){},[endpoints,counts,main_thread]{if(std::this_thread::get_id()==main_thread)counts->worker_owned=false;return endpoints;});
    Check(Wait([&]{return controller.Snapshot().initialized;}),"background initialization failed");
    auto q=controller.Snapshot().preferences;q.mode=ProcessingMode::Default;
    auto version=controller.Configure(q);Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"Default publication stalled");
    q.mode=ProcessingMode::Off;version=controller.Configure(q);Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"Off publication stalled");
    auto state=controller.Snapshot();Check(state.preferences.custom.voice_db==-3 && state.preferences.custom.boost_db==2,"mode switch lost Custom");
    q=state.preferences;
    for(int i=0;i<1000;++i){q.custom.voice_db=-20+i%27;q.mode=i%2?ProcessingMode::Default:ProcessingMode::Custom;version=controller.Configure(q);}
    q.mode=ProcessingMode::Custom;q.custom={6,-18,-20,70,3};version=controller.Configure(q);
    Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"coalesced configuration did not reach the last update");
    Check(controller.Snapshot().preferences.custom.voice_db==6 && LoadDesktopPreferences(file,{}).custom.music_db==-18,
      "latest coherent/persisted mix differs");
    q.route.physical_render_id=L"missing";version=controller.Configure(q,true);
    Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"rejected route did not acknowledge");
    Check(!controller.Snapshot().error.empty() && controller.Snapshot().preferences.route.physical_render_id==route.physical_render_id,
      "unavailable route changed the saved output");
    q=controller.Snapshot().preferences;q.route.physical_render_id=L"output-B";version=controller.Configure(q,true);
    Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"new explicit route did not apply");
    Check(controller.Snapshot().preferences.route.physical_render_id==L"output-B","explicit reselect failed");
    report<<"configuration=PASS Default_Off_preserve_Custom 1001_rapid_updates_latest_coherent tuple_persisted unavailable_route_preserved_old_choice explicit_reselection\n";
    auto begin=std::chrono::steady_clock::now();controller.Start();
    const auto enqueue_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    Check(Wait([&]{return controller.Snapshot().busy;}),"starting state was not published");
    begin=std::chrono::steady_clock::now();const auto responsive=controller.Snapshot();
    const auto snapshot_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    Check(responsive.busy && enqueue_ms<50 && snapshot_ms<50,"UI-facing operations blocked on backend start");
    Check(Wait([&]{return controller.Snapshot().live.state==LiveState::Running;}),"fake start did not complete");
    const auto previous=controller.Snapshot().preferences;q=previous;q.route.physical_render_id=L"different";version=controller.Configure(q,true);
    Check(Wait([&]{return controller.Snapshot().applied_version>=version;}),"active route reject did not finish");
    Check(controller.Snapshot().preferences.route.physical_render_id==previous.route.physical_render_id,"route changed while active");
    controller.Stop();Check(Wait([&]{return !controller.Snapshot().busy && controller.Snapshot().live.state==LiveState::Stopped;}),"background Stop failed");
    controller.Start();Check(Wait([&]{return controller.Snapshot().busy;}),"second start did not begin");
    q=controller.Snapshot().preferences;q.mode=ProcessingMode::Off;q.custom.voice_db=-7;
    controller.Configure(q);
    begin=std::chrono::steady_clock::now();controller.Shutdown();
    const auto shutdown_enqueue_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    Check(Wait([&]{return controller.Snapshot().closed;}),"shutdown during startup did not close");controller.Join();
    Check(counts->worker_owned && counts->starts==2 && counts->stops>=2 && shutdown_enqueue_ms<50,
      "backend ownership/shutdown lifecycle failed");
    Check(LoadDesktopPreferences(file,{}).custom.voice_db==-7 && LoadDesktopPreferences(file,{}).mode==ProcessingMode::Off,
      "last pending slider/mode change was lost on immediate close");
    report<<"lifecycle=PASS model_endpoint_work_off_caller_thread start_stop_restart close_during_start backend_destroyed_before_closed active_route_change_rejected"
      <<" last_pending_configuration_saved_on_close=PASS start_enqueue_ms="<<enqueue_ms<<" snapshot_during_350ms_start_ms="<<snapshot_ms<<" shutdown_enqueue_ms="<<shutdown_enqueue_ms<<'\n';
    DesktopController broken(evidence/L"malformed.ini",route,[](const auto&)->std::unique_ptr<DesktopBackend>{throw std::runtime_error("model load failed (controlled)");},[](bool){},[]{return std::vector<EndpointInfo>{};});
    Check(Wait([&]{return !broken.Snapshot().busy;}),"controlled model error stalled");
    Check(!broken.Snapshot().error.empty() && !broken.Snapshot().persistence_warning.empty(),"init/settings failures were hidden");
    broken.Shutdown();Check(Wait([&]{return broken.Snapshot().closed;}),"failed initialization could not close");broken.Join();
    report<<"initialization_failure=PASS model_error_and_corrupt_preferences_visible safe_close bad_file_untouched\nRESULT=PASS\n";
  }catch(const std::exception& e){report<<"FAILURE="<<e.what()<<"\nRESULT=FAIL\n";std::ofstream(evidence/L"desktop-verification.txt")<<report.str();std::cout<<report.str();return 1;}
  std::ofstream(evidence/L"desktop-verification.txt")<<report.str();std::cout<<report.str();return 0;
}
}
int wmain(int argc,wchar_t** argv){if(argc!=2){std::cerr<<"usage: desktop_verify evidence-dir\n";return 2;}return Run(argv[1]);}
