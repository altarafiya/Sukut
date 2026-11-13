#include "sukut-host/Engine.h"

#include <objbase.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>

#include "sukut-host/Common.h"

namespace sukut::host {

struct Engine::DeviceWatch {
  class Callback final : public IMMNotificationClient {
   public:
    Callback(HWND window, UINT message, const std::atomic<bool>* selecting) : window_(window), message_(message), selecting_(selecting) {
      CoCreateFreeThreadedMarshaler(this, &marshaller_);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
      if (!result) return E_POINTER;
      *result = nullptr;
      if (iid == __uuidof(IMarshal) && marshaller_) return marshaller_->QueryInterface(iid, result);
      if (iid != __uuidof(IUnknown) && iid != __uuidof(IMMNotificationClient)) return E_NOINTERFACE;
      *result = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override {
      // Communications-only changes must not redirect music playback.
      if (flow == eRender && role != eCommunications) Notify(id, true);
      return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override { Notify(id, false, true); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { Notify(nullptr, false); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override { Notify(id, false, state == DEVICE_STATE_ACTIVE); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
   private:
    void Notify(LPCWSTR id, bool is_default, bool arrived = false) {
      auto notice = std::make_unique<DeviceNotice>();
      if (id) notice->id = id;
      notice->default_output = is_default;
      notice->internal = selecting_->load();
      notice->arrived = arrived;
      if (PostMessageW(window_, message_, 0, reinterpret_cast<LPARAM>(notice.get()))) notice.release();
    }
    std::atomic<ULONG> references_{1};
    HWND window_; UINT message_;
    const std::atomic<bool>* selecting_;
    Microsoft::WRL::ComPtr<IUnknown> marshaller_;
  };
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  Microsoft::WRL::ComPtr<Callback> callback;
  bool registered = false;
  DeviceWatch(HWND window, UINT message, const std::atomic<bool>* selecting) {
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
      callback.Attach(new Callback(window, message, selecting));
      registered = SUCCEEDED(enumerator->RegisterEndpointNotificationCallback(callback.Get()));
    }
  }
  ~DeviceWatch() { if (registered) enumerator->UnregisterEndpointNotificationCallback(callback.Get()); }
};

void Engine::WatchDevices(HWND window, UINT message, const std::atomic<bool>* selecting) { device_watch_ = std::make_unique<DeviceWatch>(window, message, selecting); }

Engine::Engine() : buffer_(64 * 1024) {
  worker_ = std::thread([this] { Loop(); });
}

Engine::~Engine() { Stop(); }

void Engine::Post(std::function<void()> work) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    jobs_.push_back(std::move(work));
  }
  wake_.notify_one();
}

void Engine::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_one();
  if (worker_.joinable()) worker_.join();
  if (module_) { FreeLibrary(module_); module_ = nullptr; }
}

void Engine::Loop() {
  // The engine's COM audio objects belong to this single-threaded apartment.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetThreadDescription(GetCurrentThread(), L"Sukut native controller");
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
      if (jobs_.empty()) break;  // stopping and drained
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
  device_watch_.reset();
  if (SUCCEEDED(com)) CoUninitialize();
}

bool Engine::Load(const std::filesystem::path& dll, std::string& error) {
  // Resolve the engine's own dependencies (VC runtime) from its folder.
  module_ = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!module_) { error = "Sukut audio engine is missing: " + Narrow(dll.wstring()); return false; }
  const auto get = [&](const char* name) { return GetProcAddress(module_, name); };
  create_ = reinterpret_cast<CreateFn>(get("Sukut_CreateWithPaths"));
  read_since_ = reinterpret_cast<ReadSinceFn>(get("Sukut_ReadSince"));
  apps_ = reinterpret_cast<AppsFn>(get("Sukut_Apps"));
  error_ = reinterpret_cast<ErrorFn>(get("Sukut_Error"));
  select_ = reinterpret_cast<SelectFn>(get("Sukut_SelectPreset"));
  refresh_ = reinterpret_cast<HandleFn>(get("Sukut_Refresh"));
  destroy_ = reinterpret_cast<HandleFn>(get("Sukut_Destroy"));
  buffering_ = reinterpret_cast<SetIntFn>(get("Sukut_SetBuffering"));
  amplification_ = reinterpret_cast<SetIntFn>(get("Sukut_SetAmplification"));
  cable_ = reinterpret_cast<SetIntFn>(get("Sukut_SetCablePassthroughConfirmed"));
  if (!create_ || !read_since_ || !apps_ || !error_ || !select_ || !refresh_ || !destroy_ || !buffering_ || !amplification_ || !cable_) {
    error = "Sukut audio engine is incomplete or from another version.";
    return false;
  }
  return true;
}

std::string Engine::Reply(const std::function<int(char*, int)>& call, std::string& error) {
  // The engine returns the required size when the buffer is too small.
  for (int attempt = 0; attempt < 4; ++attempt) {
    const int needed = call(buffer_.data(), static_cast<int>(buffer_.size()));
    if (needed < 0) {
      const int length = error_(buffer_.data(), static_cast<int>(buffer_.size()));
      error = length > 0 && length <= static_cast<int>(buffer_.size()) ? std::string(buffer_.data()) : "audio engine error";
      return {};
    }
    if (needed <= static_cast<int>(buffer_.size())) return std::string(buffer_.data(), static_cast<std::size_t>(std::max(0, needed - 1)));
    buffer_.resize(static_cast<std::size_t>(needed) + 4096);
  }
  error = "audio engine reply is too large";
  return {};
}

bool Engine::Create(const std::filesystem::path& data_root, const std::filesystem::path& resource_root, std::string& error) {
  context_ = create_(data_root.c_str(), resource_root.c_str());
  if (!context_) {
    std::string ignored;
    error = Reply([this](char* b, int n) { return error_(b, n); }, ignored);
    if (error.empty()) error = "Sukut audio engine could not start.";
    return false;
  }
  return true;
}

std::string Engine::Read(std::int64_t endpoints_version, std::string& error) {
  if (!context_) { error = "audio engine is not running"; return {}; }
  return Reply([&](char* b, int n) { return read_since_(context_, endpoints_version, b, n); }, error);
}

std::string Engine::Apps(std::string& error) {
  return Reply([&](char* b, int n) { return apps_(b, n); }, error);
}

bool Engine::Select(const Selection& s, std::string& error) {
  // Same sequence and stem gains as the window's Engine.Select and
  // AudioModePreset: voice 0 dB; music muted in every filtering mode.
  const auto failed = [&] {
    std::string ignored;
    error = Reply([this](char* b, int n) { return error_(b, n); }, ignored);
    if (error.empty()) error = "audio engine error";
    return false;
  };
  if (!context_) { error = "audio engine is not running"; return false; }
  if (cable_(context_, s.cable_confirmed ? 1 : 0) == 0) return failed();
  if (buffering_(context_, std::clamp(s.buffering, 0, 2)) == 0) return failed();
  if (s.restart && s.mode != 0 &&
      select_(context_, 0, s.source.c_str(), s.capture.c_str(), s.output.c_str(), s.pid, 0, 0, 0) == 0) return failed();
  const auto gains=cleanwave::ModeGains(s.mode,s.optimized_background_db,s.optimized_mix);
  if (select_(context_, s.mode, s.source.c_str(), s.capture.c_str(), s.output.c_str(), s.pid,
              static_cast<float>(gains.voice_db),static_cast<float>(gains.music_db),static_cast<float>(gains.background_db)) == 0) return failed();
  return true;
}

void Engine::Refresh() { if (context_) refresh_(context_); }

bool Engine::SetAmplification(int db, std::string& error) {
  if (!context_) { error = "audio engine is not running"; return false; }
  if (amplification_(context_, std::clamp(db, 0, 12)) != 0) return true;
  std::string ignored;
  error = Reply([this](char* b, int n) { return error_(b, n); }, ignored);
  return false;
}

void Engine::Destroy() {
  device_watch_.reset();
  // Stops processing and restores the original audio routes.
  if (context_) { destroy_(context_); context_ = nullptr; }
}

EngineStatus EngineStatus::Parse(const std::string& text, const EngineStatus& previous) {
  EngineStatus s;
  s.endpoints = previous.endpoints;
  s.endpoints_version = previous.endpoints_version;
  const auto parsed = json::Parse(text);
  if (!parsed) return s;
  const auto& v = *parsed;
  s.valid = true;
  s.initialized = v.Num("initialized") != 0;
  s.busy = v.Num("busy") != 0;
  s.state = static_cast<int>(v.Num("state"));
  s.paused = v.Num("paused") != 0;
  s.standby = v.Num("standby") != 0;
  s.pipeline_active = v.Num("pipelineActive") != 0;
  s.controller_age_ms = static_cast<std::int64_t>(v.Num("controllerAgeMs"));
  s.error = v.Str("error");
  s.kernels = v.Str("kernels");
  if (const auto* list = v.Find("endpoints"); list && list->kind == json::Value::Kind::Array) {
    s.endpoints.clear();
    for (const auto& e : list->items)
      s.endpoints.push_back({e.Str("id"), e.Str("name"), static_cast<int>(e.Num("flow")), e.Num("virtual") != 0});
    s.endpoints_version = static_cast<std::int64_t>(v.Num("endpointsVersion", -1));
  }
  return s;
}

}  // namespace sukut::host
