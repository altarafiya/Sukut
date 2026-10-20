#include "audio/WinAudio.h"
#include "audio/AudioEndpoints.h"

#include <endpointvolume.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <propvarutil.h>

#include <algorithm>
#include <cwctype>

namespace cleanwave {
using namespace audio_detail;
namespace {
std::wstring Property(IPropertyStore* store, const PROPERTYKEY& key) {
  PROPVARIANT value;
  PropVariantInit(&value);
  std::wstring result;
  if (SUCCEEDED(store->GetValue(key, &value))) {
    if (value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal;
    else if (value.vt == VT_CLSID && value.puuid) {
      wchar_t buffer[64]; StringFromGUID2(*value.puuid, buffer, 64); result = buffer;
    }
  }
  PropVariantClear(&value);
  return result;
}
std::wstring DefaultId(IMMDeviceEnumerator* e, EDataFlow flow, ERole role) {
  ComPtr<IMMDevice> device;
  if (FAILED(e->GetDefaultAudioEndpoint(flow, role, &device))) return {};
  wchar_t* id = nullptr;
  CheckHr(device->GetId(&id), "default GetId");
  std::wstring result(id); CoTaskMemFree(id); return result;
}
EndpointInfo Inspect(IMMDevice* device, IMMDeviceEnumerator* enumerator) {
  EndpointInfo result;
  wchar_t* id = nullptr;
  CheckHr(device->GetId(&id), "GetId");
  result.id = id; CoTaskMemFree(id);
  DWORD state = 0;
  CheckHr(device->GetState(&state), "GetState");
  result.state = state;
  ComPtr<IMMEndpoint> endpoint;
  CheckHr(device->QueryInterface(IID_PPV_ARGS(&endpoint)), "IMMEndpoint");
  EDataFlow flow;
  CheckHr(endpoint->GetDataFlow(&flow), "GetDataFlow");
  result.flow = flow == eCapture ? EndpointFlow::Capture : EndpointFlow::Render;
  ComPtr<IPropertyStore> store;
  CheckHr(device->OpenPropertyStore(STGM_READ, &store), "OpenPropertyStore");
  result.name = Property(store.Get(), PKEY_Device_FriendlyName);
  result.container_id = Property(store.Get(), PKEY_Device_ContainerId);
  ComPtr<IDeviceTopology> topology;
  if (SUCCEEDED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, &topology))) {
    ComPtr<IConnector> connector;
    if (SUCCEEDED(topology->GetConnector(0, &connector))) {
      wchar_t* connected = nullptr;
      if (SUCCEEDED(connector->GetDeviceIdConnectedTo(&connected))) {
        result.adapter_id = connected;
        CoTaskMemFree(connected);
        ComPtr<IMMDevice> adapter;
        if (SUCCEEDED(enumerator->GetDevice(result.adapter_id.c_str(), &adapter))) {
          ComPtr<IPropertyStore> adapter_store;
          if (SUCCEEDED(adapter->OpenPropertyStore(STGM_READ, &adapter_store))) {
            result.adapter_instance_id = Property(adapter_store.Get(), PKEY_Device_InstanceId);
          }
        }
      }
    }
  }
  result.default_console = result.id == DefaultId(enumerator, flow, eConsole);
  result.default_multimedia = result.id == DefaultId(enumerator, flow, eMultimedia);
  result.default_communications = result.id == DefaultId(enumerator, flow, eCommunications);
  if (result.state != DEVICE_STATE_ACTIVE) return result;
  try {
    ComPtr<IAudioClient> client;
    CheckHr(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "Activate IAudioClient");
    WAVEFORMATEX* format = nullptr;
    CheckHr(client->GetMixFormat(&format), "GetMixFormat");
    result.mix_rate = format->nSamplesPerSec;
    result.channels = format->nChannels;
    result.bits = result.valid_bits = format->wBitsPerSample;
    result.mix_float = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
      const auto* extended = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
      result.valid_bits = extended->Samples.wValidBitsPerSample;
      result.channel_mask = extended->dwChannelMask;
      result.mix_float = extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    CoTaskMemFree(format);
    ComPtr<IAudioEndpointVolume> volume;
    CheckHr(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume), "Activate endpoint volume");
    BOOL muted = FALSE;
    CheckHr(volume->GetMute(&muted), "GetMute");
    result.muted = muted != FALSE;
    CheckHr(volume->GetMasterVolumeLevelScalar(&result.volume), "GetMasterVolumeLevelScalar");
  } catch (const std::exception& e) {
    const std::string message = e.what();
    result.error.assign(message.begin(), message.end());
  }
  return result;
}
std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
  return value;
}
}

std::vector<EndpointInfo> EnumerateAudioEndpoints() {
  Apartment apartment;
  auto enumerator = Enumerator();
  ComPtr<IMMDeviceCollection> devices;
  CheckHr(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &devices), "EnumAudioEndpoints");
  UINT count = 0; CheckHr(devices->GetCount(&count), "GetCount");
  std::vector<EndpointInfo> result;
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    CheckHr(devices->Item(i, &device), "Item");
    result.push_back(Inspect(device.Get(), enumerator.Get()));
  }
  return result;
}
EndpointInfo InspectAudioEndpoint(const std::wstring& id) {
  Apartment apartment;
  auto enumerator = Enumerator();
  ComPtr<IMMDevice> device;
  CheckHr(enumerator->GetDevice(id.c_str(), &device), "GetDevice");
  return Inspect(device.Get(), enumerator.Get());
}

void ValidateAudioRoute(const AudioRoute& route, const std::vector<EndpointInfo>& endpoints) {
  const auto find = [&](const std::wstring& id, EndpointFlow flow) -> const EndpointInfo& {
    const auto it = std::find_if(endpoints.begin(), endpoints.end(), [&](const auto& e) { return e.id == id; });
    if (it == endpoints.end() || it->flow != flow || it->state != DEVICE_STATE_ACTIVE || !it->error.empty()) {
      throw std::runtime_error("route endpoint is missing, inactive, unreadable or has wrong direction");
    }
    return *it;
  };
  const auto& source = find(route.source_render_id, EndpointFlow::Render);
  const auto& capture = find(route.capture_id, EndpointFlow::Capture);
  const auto& output = find(route.physical_render_id, EndpointFlow::Render);
  if (source.id == output.id || capture.id == output.id) throw std::runtime_error("feedback route rejected");
  const auto source_name = Lower(source.name), capture_name = Lower(capture.name), output_name = Lower(output.name);
  if (source_name.find(L"cable input") == std::wstring::npos ||
      capture_name.find(L"cable output") == std::wstring::npos ||
      source_name.find(L"vb-audio") == std::wstring::npos ||
      capture_name.find(L"vb-audio") == std::wstring::npos) {
    throw std::runtime_error("unverified virtual pair; baseline requires VB-Audio CABLE Input/Output");
  }
  if (source.adapter_instance_id.empty() || capture.adapter_instance_id.empty() ||
      Lower(source.adapter_instance_id) != Lower(capture.adapter_instance_id)) {
    throw std::runtime_error("virtual endpoint adapter pairing could not be verified");
  }
  if (output.adapter_instance_id.empty() || Lower(output.adapter_instance_id) == Lower(capture.adapter_instance_id) ||
      output_name.find(L"vb-audio") != std::wstring::npos) {
    throw std::runtime_error("output must be outside the virtual capture adapter");
  }
  const auto output_instance = Lower(output.adapter_instance_id);
  if (output_instance.rfind(L"usb\\", 0) != 0 && output_instance.rfind(L"hdaudio\\", 0) != 0) {
    throw std::runtime_error("unverified physical output bus; baseline supports USB/HDAUDIO adapters");
  }
}

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (count == 0) throw std::runtime_error("invalid Unicode endpoint text");
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), count, nullptr, nullptr);
  return result;
}
std::string EscapeJson(const std::string& text) {
  std::ostringstream escaped;
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') escaped << '\\' << c;
    else if (c < 32) escaped << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<unsigned>(c);
    else escaped << c;
  }
  return escaped.str();
}
}  // namespace cleanwave
