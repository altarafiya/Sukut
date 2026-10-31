#include "audio/AudioEndpoints.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace cleanwave;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F action, const char* message, const char* expected = nullptr) {
  try { action(); } catch(const std::exception& error) {
    if(expected) Check(std::string(error.what()).find(expected) != std::string::npos, "route error did not identify the failing endpoint");
    return;
  }
  throw std::runtime_error(message);
}
int main(int argc, char**) {
  try {
    EndpointInfo source; source.id=L"source"; source.name=L"CABLE Input (VB-Audio Virtual Cable)";
    source.state=1; source.flow=EndpointFlow::Render; source.adapter_instance_id=L"ROOT\\MEDIA\\0000";
    auto capture=source; capture.id=L"capture"; capture.name=L"CABLE Output (VB-Audio Virtual Cable)"; capture.flow=EndpointFlow::Capture;
    EndpointInfo output; output.id=L"output"; output.name=L"Headphones"; output.state=1; output.flow=EndpointFlow::Render;
    const AudioRoute route{source.id,capture.id,output.id};
    for(const auto& bus : {L"USB\\AUDIO",L"HDAUDIO\\DEVICE",L"BTHENUM\\A2DP",L"BTHHFENUM\\AUDIO",L"BTHLEDEVICE\\AUDIO",L"bthenum\\lowercase"}) {
      output.adapter_instance_id=bus; ValidateAudioRoute(route,{source,capture,output});
    }
    output.adapter_instance_id=L"ROOT\\MEDIA\\9999";
    Reject([&]{ValidateAudioRoute(route,{source,capture,output});},"arbitrary software bus accepted");
    output.adapter_instance_id=L"BTHENUM\\A2DP";
    auto disconnected=output; disconnected.state=8;
    Reject([&]{ValidateAudioRoute(route,{source,capture,disconnected});},"disconnected Bluetooth accepted","Selected audio output is disconnected");
    auto unreadable=output; unreadable.error=L"GetMixFormat failed";
    Reject([&]{ValidateAudioRoute(route,{source,capture,unreadable});},"unreadable endpoint accepted","GetMixFormat failed");
    auto wrong=output; wrong.flow=EndpointFlow::Capture;
    Reject([&]{ValidateAudioRoute(route,{source,capture,wrong});},"Bluetooth microphone accepted as output","wrong audio direction");
    Reject([&]{ValidateAudioRoute({source.id,capture.id,source.id},{source,capture,output});},"feedback route accepted");
    auto unpaired=capture; unpaired.adapter_instance_id=L"ROOT\\MEDIA\\0001";
    Reject([&]{ValidateAudioRoute(route,{source,unpaired,output});},"unverified virtual pair accepted");
    auto missing=capture; missing.state=8;
    Reject([&]{ValidateAudioRoute(route,{source,missing,output});},"missing capture accepted","VB-CABLE output is disconnected");
    auto speakers=output;speakers.id=L"speakers";speakers.adapter_instance_id=L"HDAUDIO\\DEVICE";
    auto newest=output;newest.id=L"new-bluetooth";newest.default_multimedia=true;
    Check(ChooseRestoreOutput(output.id,source.id,{source,capture,output,speakers,newest})==output.id,"connected saved restore target changed");
    auto saved_virtual=output;saved_virtual.id=L"saved-app-output";saved_virtual.name=L"Previous app output";saved_virtual.adapter_instance_id=L"ROOT\\MEDIA\\9999";
    Check(ChooseRestoreOutput(saved_virtual.id,source.id,{source,capture,saved_virtual,speakers})==saved_virtual.id,"saved Windows choice was unnecessarily replaced by a physical fallback");
    Check(ChooseRestoreOutput(output.id,source.id,{source,capture,disconnected,speakers,newest})==newest.id,"stale saved endpoint did not prefer Windows' new physical output");
    Check(ChooseRestoreOutput(output.id,source.id,{source,capture,disconnected,speakers})==speakers.id,"disconnection stranded virtual cable instead of connected physical fallback");
    Reject([&]{ChooseRestoreOutput(output.id,source.id,{source,capture,disconnected});},"no-output restoration silently discarded its journal","restoration is waiting");
    Reject([&]{ChooseRestoreOutput(output.id,source.id,{source,capture,disconnected,saved_virtual});},"fallback selected an arbitrary virtual output");
    std::cout << "PASS USB/HDAUDIO/Bluetooth route validation; virtual feedback, pairing, direction, inactive and unreadable guards\n";
    if(argc>1) {
      const auto endpoints=EnumerateAudioEndpoints();
      const auto find=[&](EndpointFlow flow,const std::wstring& name) { return std::find_if(endpoints.begin(),endpoints.end(),[&](const auto& e) {
        return e.flow==flow && e.state==1 && e.name.find(name)!=std::wstring::npos && e.name.find(L"VB-Audio")!=std::wstring::npos; }); };
      const auto render=find(EndpointFlow::Render,L"CABLE Input"), input=find(EndpointFlow::Capture,L"CABLE Output");
      Check(render!=endpoints.end() && input!=endpoints.end(),"connected-device audit requires installed active VB-CABLE pair");
      int bluetooth=0;
      for(const auto& e:endpoints) if(e.state==1 && e.flow==EndpointFlow::Render && e.adapter_instance_id.rfind(L"BTH",0)==0) {
        ValidateAudioRoute({render->id,input->id,e.id},endpoints); ++bluetooth;
        std::cout << "PASS connected Bluetooth endpoint: " << Utf8(e.name) << '\n';
      }
      Check(bluetooth>0,"no active Bluetooth output to validate");
      std::cout << "Read-only endpoint validation: no streams, routes or volumes changed\n";
    }
    return 0;
  } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
