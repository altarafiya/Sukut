#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include "audio/AudioEndpoints.h"
#include "audio/WasapiAdapters.h"

using namespace cleanwave;
int wmain(int argc, wchar_t** argv) {
  if (argc != 5) {
    std::cerr << "usage: cleanwave_live_source virtual-render-id rate seconds report.txt\n"; return 2;
  }
  WasapiRender source(argv[1], static_cast<std::uint32_t>(std::stoul(argv[2])));
  try {
    const auto endpoint = InspectAudioEndpoint(argv[1]);
    if (endpoint.flow != EndpointFlow::Render ||
        Utf8(endpoint.name).find("CABLE Input") == std::string::npos)
      throw std::runtime_error("test source is restricted to CABLE Input");
    const unsigned rate = source.SampleRate();
    const unsigned seconds = static_cast<unsigned>(std::stoul(argv[3]));
    if (seconds == 0 || seconds > 120) throw std::invalid_argument("source duration must be 1..120 seconds");
    source.Open({});
    std::uint64_t cursor = 0;
    std::vector<std::int16_t> packet(rate / 50 * 2);
    const auto begin = std::chrono::steady_clock::now();
    constexpr double pi = 3.14159265358979323846;
    while (cursor < static_cast<std::uint64_t>(seconds) * rate) {
      for (std::size_t i = 0; i < packet.size() / 2; ++i) {
        const double t = static_cast<double>(cursor + i) / rate;
        const double envelope = t < .25 ? 0 : .75 + .25 * std::sin(2 * pi * 2.3 * t);
        packet[i * 2] = static_cast<std::int16_t>(envelope *
            (3000 * std::sin(2 * pi * 337 * t) + 700 * std::sin(2 * pi * 17333 * t)));
        packet[i * 2 + 1] = static_cast<std::int16_t>(envelope *
            (2300 * std::cos(2 * pi * 719 * t) - 500 * std::sin(2 * pi * 15891 * t)));
      }
      std::size_t offset = 0;
      while (offset < packet.size() / 2) {
        if (std::chrono::steady_clock::now() - begin > std::chrono::seconds(seconds + 3))
          throw std::runtime_error("live source render deadline exceeded");
        offset += source.Write(std::span(packet).subspan(offset * 2), cursor + offset, {});
      }
      cursor += packet.size() / 2;
    }
    source.Finish({}); source.Close();
    const auto s = source.Snapshot();
    std::ofstream out{std::filesystem::path(argv[4])};
    if (!out) throw std::runtime_error("cannot write source evidence");
    out << "source=independent_live_WASAPI_process destination=CABLE_Input\n"
        << "client_rate=" << rate << " mix_rate=" << s.mix_rate << " frames=" << s.frames
        << " buffer_frames=" << s.buffer_frames << " empty_padding_observations=" << s.empty_render_observations
        << " wait_timeouts=" << s.wait_timeouts << " open=" << s.open << "\nRESULT=PASS\n";
    std::cout << "source_frames=" << cursor << " rate=" << rate << " RESULT=PASS\n";
    return 0;
  } catch (const std::exception& e) {
    source.Close(); std::cerr << "live source failure: " << e.what() << '\n'; return 1;
  }
}
