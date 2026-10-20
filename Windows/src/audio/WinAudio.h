#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cleanwave::audio_detail {
template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
inline void CheckHr(HRESULT hr, const char* operation) {
  if (FAILED(hr)) {
    std::ostringstream message;
    message << operation << " failed HRESULT=0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr);
    throw std::runtime_error(message.str());
  }
}
class Apartment final {
 public:
  Apartment() { CheckHr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx(STA)"); }
  ~Apartment() { CoUninitialize(); }
  Apartment(const Apartment&) = delete;
};
class Event final {
 public:
  explicit Event(bool manual = false) : handle_(CreateEventW(nullptr, manual, FALSE, nullptr)) {
    if (!handle_) throw std::runtime_error("CreateEvent failed");
  }
  ~Event() { CloseHandle(handle_); }
  HANDLE Get() const { return handle_; }
  void Signal() const { SetEvent(handle_); }
  Event(const Event&) = delete;
 private:
  HANDLE handle_;
};
inline ComPtr<IMMDeviceEnumerator> Enumerator() {
  ComPtr<IMMDeviceEnumerator> result;
  CheckHr(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&result)), "MMDeviceEnumerator");
  return result;
}
inline ComPtr<IMMDevice> Device(const std::wstring& id) {
  auto enumerator = Enumerator();
  ComPtr<IMMDevice> device;
  CheckHr(enumerator->GetDevice(id.c_str(), &device), "GetDevice");
  return device;
}
inline void CheckFlow(IMMDevice* device, EDataFlow expected) {
  ComPtr<IMMEndpoint> endpoint;
  CheckHr(device->QueryInterface(IID_PPV_ARGS(&endpoint)), "IMMEndpoint");
  EDataFlow flow;
  CheckHr(endpoint->GetDataFlow(&flow), "GetDataFlow");
  DWORD state = 0;
  CheckHr(device->GetState(&state), "GetState");
  if (flow != expected || state != DEVICE_STATE_ACTIVE) {
    throw std::runtime_error("endpoint has wrong direction or is inactive");
  }
}
}  // namespace cleanwave::audio_detail
