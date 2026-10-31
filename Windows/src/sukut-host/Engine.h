#pragma once

#include <windows.h>

#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sukut-host/Json.h"

namespace sukut::host {

struct DeviceNotice { std::wstring id; bool default_output = false, internal = false, arrived = false; };

// The native engine (SukutEngine.dll), driven from one STA worker
// thread exactly as the WinUI window used to drive it. All calls are queued
// and run in order; callers either wait (pipe thread) or get a callback.
class Engine {
 public:
  struct Selection {
    int mode = 0;
    std::wstring source, capture, output;
    std::uint32_t pid = 0;
    int buffering = 0;
    bool cable_confirmed = false;
    int optimized_background_db = 0;
    bool restart = false;
  };

  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Worker-side calls (use Run/Post to execute them on the worker).
  bool Load(const std::filesystem::path& dll, std::string& error);
  bool Create(const std::filesystem::path& data_root, const std::filesystem::path& resource_root, std::string& error);
  std::string Read(std::int64_t endpoints_version, std::string& error);
  std::string Apps(std::string& error);
  bool Select(const Selection& selection, std::string& error);
  void Refresh();
  void WatchDevices(HWND window, UINT message, const std::atomic<bool>* selecting);
  bool SetAmplification(int db, std::string& error);
  void Destroy();
  [[nodiscard]] bool Ready() const { return context_ != nullptr; }

  template <typename Result>
  Result Run(std::function<Result()> work) {
    auto task = std::make_shared<std::packaged_task<Result()>>(std::move(work));
    auto future = task->get_future();
    Post([task] { (*task)(); });
    return future.get();
  }
  void Post(std::function<void()> work);
  void Stop();

 private:
  void Loop();
  std::string Reply(const std::function<int(char*, int)>& call, std::string& error);

  HMODULE module_ = nullptr;
  void* context_ = nullptr;
  using CreateFn = void*(__cdecl*)(const wchar_t*, const wchar_t*);
  using ReadSinceFn = int(__cdecl*)(void*, std::int64_t, char*, int);
  using AppsFn = int(__cdecl*)(char*, int);
  using ErrorFn = int(__cdecl*)(char*, int);
  using SelectFn = int(__cdecl*)(void*, int, const wchar_t*, const wchar_t*, const wchar_t*, unsigned, float, float, float);
  using HandleFn = void(__cdecl*)(void*);
  using SetIntFn = int(__cdecl*)(void*, int);
  CreateFn create_ = nullptr;
  ReadSinceFn read_since_ = nullptr;
  AppsFn apps_ = nullptr;
  ErrorFn error_ = nullptr;
  SelectFn select_ = nullptr;
  HandleFn refresh_ = nullptr, destroy_ = nullptr;
  SetIntFn buffering_ = nullptr, amplification_ = nullptr, cable_ = nullptr;
  std::vector<char> buffer_;

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
  std::thread worker_;
  struct DeviceWatch;
  std::unique_ptr<DeviceWatch> device_watch_;
};

// The fields of the engine status JSON the host needs.
struct EngineStatus {
  bool valid = false, initialized = false, busy = false, paused = false, standby = false, pipeline_active = false;
  int state = 0;  // 0 stopped, 1 running, 2 faulted
  std::int64_t endpoints_version = -1, controller_age_ms = 0;
  std::string error, kernels;
  struct Endpoint { std::string id, name; int flow = 0; bool is_virtual = false; };
  std::vector<Endpoint> endpoints;  // kept from the last full list
  static EngineStatus Parse(const std::string& text, const EngineStatus& previous);
};

}  // namespace sukut::host
