#include <filesystem>
#include <fstream>
#include <iostream>
#include "audio/AudioEndpoints.h"
#include "audio/WinAudio.h"

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: cleanwave_audio_probe <endpoints.json>");
    cleanwave::audio_detail::Apartment apartment;
    const auto endpoints = cleanwave::EnumerateAudioEndpoints();
    std::ofstream out{std::filesystem::path(argv[1])};
    if (!out) throw std::runtime_error("cannot open endpoint report");
    out << "[\n";
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
      const auto& e = endpoints[i];
      const auto quote = [](const std::wstring& value) { return '"' + cleanwave::EscapeJson(cleanwave::Utf8(value)) + '"'; };
      out << " {\"id\":" << quote(e.id) << ",\"name\":" << quote(e.name)
          << ",\"container\":" << quote(e.container_id) << ",\"error\":" << quote(e.error)
          << ",\"adapter\":" << quote(e.adapter_id)
          << ",\"adapter_instance\":" << quote(e.adapter_instance_id)
          << ",\"flow\":\"" << (e.flow == cleanwave::EndpointFlow::Capture ? "Capture" : "Render")
          << "\",\"state\":" << e.state << ",\"mix_rate\":" << e.mix_rate
          << ",\"channels\":" << e.channels << ",\"bits\":" << e.bits
          << ",\"valid_bits\":" << e.valid_bits << ",\"channel_mask\":" << e.channel_mask
          << ",\"float\":" << (e.mix_float ? "true" : "false")
          << ",\"default_console\":" << (e.default_console ? "true" : "false")
          << ",\"default_multimedia\":" << (e.default_multimedia ? "true" : "false")
          << ",\"default_communications\":" << (e.default_communications ? "true" : "false")
          << ",\"muted\":" << (e.muted ? "true" : "false") << ",\"volume\":" << e.volume;
      // Query driver capabilities only. Do not initialize/start a stream or
      // alter system formats, effects, routing, volume or engine periodicity.
      if (e.state == 1) {
        using namespace cleanwave::audio_detail;
        try {
          auto device=Device(e.id);ComPtr<IAudioClient3> client;
          CheckHr(device->Activate(__uuidof(IAudioClient3),CLSCTX_ALL,nullptr,&client),"Activate IAudioClient3");
          WAVEFORMATEX* format=nullptr;CheckHr(client->GetMixFormat(&format),"GetMixFormat");
          UINT32 normal=0,fundamental=0,minimum=0,maximum=0;const auto rate=format->nSamplesPerSec;
          const HRESULT hr=client->GetSharedModeEnginePeriod(format,&normal,&fundamental,&minimum,&maximum);
          CoTaskMemFree(format);CheckHr(hr,"GetSharedModeEnginePeriod");
          out << ",\"engine_period_rate\":" << rate << ",\"engine_default_frames\":" << normal
              << ",\"engine_fundamental_frames\":" << fundamental << ",\"engine_min_frames\":" << minimum
              << ",\"engine_max_frames\":" << maximum;
          std::cout << cleanwave::Utf8(e.name) << " engine_default_ms=" << normal*1000.0/rate
                    << " engine_min_ms=" << minimum*1000.0/rate << '\n';
        }catch(const std::exception& error){out << ",\"engine_period_error\":\"" << cleanwave::EscapeJson(error.what()) << "\"";}
      }
      out << '}';
      out << (i + 1 == endpoints.size() ? "\n" : ",\n");
      if (e.state == 1) std::cout << cleanwave::Utf8(e.name) << " | " << e.mix_rate << "Hz "
          << e.channels << "ch " << e.bits << "bits | container=" << cleanwave::Utf8(e.container_id) << '\n';
    }
    out << "]\n";
    if (!out) throw std::runtime_error("endpoint report write failed");
    std::cout << "endpoints=" << endpoints.size() << " RESULT=PASS\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
