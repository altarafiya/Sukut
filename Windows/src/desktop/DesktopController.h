#pragma once
#include <condition_variable>
#include <functional>
#include <optional>
#include <thread>
#include "desktop/DesktopPreferences.h"

namespace cleanwave {
struct DesktopSnapshot {
  DesktopPreferences preferences;
  std::vector<EndpointInfo> endpoints;
  LiveSnapshot live;
  std::string error, persistence_warning;
  std::wstring status=L"Preparing CleanWave…";
  bool initialized=false, busy=true, closed=false;
  std::uint64_t applied_version=0;
  std::uint64_t endpoints_version=0;
  std::uint64_t completed_stop=0;
  std::chrono::steady_clock::time_point published{};
};
// Backend interface keeps presenter/lifecycle tests independent of hardware.
class DesktopBackend {
 public:
  virtual ~DesktopBackend()=default;
  virtual void Configure(const DesktopPreferences& p)=0;
  virtual void SelectRoute(const AudioRoute& route)=0;
  virtual void Start()=0;
  virtual bool Stop()=0;
  virtual LiveSnapshot Snapshot()=0;
};
class NativeDesktopBackend final : public DesktopBackend {
 public:
  NativeDesktopBackend(const std::filesystem::path& root, const DesktopPreferences& preferences,
                       std::filesystem::path diagnostic_path = {});
  void Configure(const DesktopPreferences& p) override;
  void SelectRoute(const AudioRoute& route) override { session_.SelectRoute(route); }
  void Start() override {session_.Start();}
  bool Stop() override {return session_.Stop();}
  LiveSnapshot Snapshot() override;
 private:
  LiveSession session_;
  std::filesystem::path diagnostic_path_;
  std::chrono::steady_clock::time_point last_diagnostic_{};
  LiveState last_state_ = LiveState::Faulted;
  std::uint64_t last_generation_ = 0;
};
class DesktopController final {
 public:
  using Factory=std::function<std::unique_ptr<DesktopBackend>(const DesktopPreferences&)>;
  using EndpointReader=std::function<std::vector<EndpointInfo>()>;
  DesktopController(std::filesystem::path settings, AudioRoute fallback, Factory factory,
                    std::function<void(bool)> notify, EndpointReader reader=EnumerateAudioEndpoints);
  ~DesktopController();
  DesktopSnapshot Snapshot() const;
  std::uint64_t Configure(DesktopPreferences p, bool route_change=false);
  void Start(); std::uint64_t Stop(); void Refresh(); void Shutdown();
  void Join(); // only after closed notification; never block UI during operations
 private:
  void Run(); void Publish(); void UpdateLive();
  struct Update {DesktopPreferences preferences; bool route_change; std::uint64_t version;};
  std::filesystem::path settings_;
  AudioRoute fallback_;
  Factory factory_;
  std::function<void(bool)> notify_;
  EndpointReader read_endpoints_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  DesktopSnapshot snapshot_;
  std::optional<Update> update_;
  std::thread worker_;
  std::unique_ptr<DesktopBackend> backend_; // worker-owned, destroyed before closed notification
  bool start_=false, stop_=false, refresh_=false, shutdown_=false;
  std::uint64_t version_=0,stop_version_=0;
  DesktopSnapshot working_; // worker-owned
};
} // namespace cleanwave
