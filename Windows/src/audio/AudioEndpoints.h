#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cleanwave {

enum class EndpointFlow { Capture, Render };
struct EndpointInfo {
  std::wstring id, name, container_id, adapter_id, adapter_instance_id, error;
  EndpointFlow flow = EndpointFlow::Render;
  std::uint32_t state = 0;
  std::uint32_t mix_rate = 0, channels = 0, bits = 0, valid_bits = 0;
  std::uint32_t channel_mask = 0;
  bool mix_float = false;
  bool default_console = false, default_multimedia = false, default_communications = false;
  bool muted = false;
  float volume = 0;
};

struct AudioRoute {
  std::wstring source_render_id, capture_id, physical_render_id;
};

std::vector<EndpointInfo> EnumerateAudioEndpoints();
EndpointInfo InspectAudioEndpoint(const std::wstring& id);
// Explicit virtual pair + distinct output. This baseline accepts only the
// installed VB-Audio CABLE pair; additional drivers require their own mapping.
void ValidateAudioRoute(const AudioRoute& route,
                        const std::vector<EndpointInfo>& endpoints);
std::string Utf8(const std::wstring& text);
std::string EscapeJson(const std::string& text);

}  // namespace cleanwave
