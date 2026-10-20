#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace cleanwave::sukut {
struct SourceApp { unsigned pid; std::wstring name, path; std::uint64_t started; };
std::vector<SourceApp> SourceApps();
std::vector<SourceApp> LiveSourceProcesses(const std::wstring& path);
void ReturnToPhysical(const std::wstring& cable,const std::wstring& physical);
void SelectPhysicalOutput(const std::wstring& cable,const std::wstring& physical,unsigned pid);
class AudioRouting final {
 public:
  explicit AudioRouting(std::filesystem::path journal);
  ~AudioRouting();
  void Begin(const std::wstring& cable,const std::wstring& physical,unsigned pid,const std::wstring& owner);
  void Restore(const std::wstring& expected_owner={},bool defer_missing_source=true);
  void RestorePending(unsigned active_pid=0,bool all_system_active=false); // idle/controller thread only
  void PollVolume(); // controller thread only; never an audio worker
  void CheckRoute(); // detects external rerouting without stealing it back
 private:
  struct VolumeState { float level=1; bool muted=false; };
  struct VolumeBridge;
  void Save();
  void Load();
  std::filesystem::path journal_;
  std::wstring cable_,physical_,previous_[2],process_path_;
  std::wstring defaults_[2];
  unsigned pid_=0;
  std::uint64_t process_started_=0;
  bool owned_=false;
  bool all_system_=true;
  SourceApp last_source_{};
  bool defaults_owned_=false;
  std::unique_ptr<VolumeBridge> volume_;
  VolumeState cable_before_,physical_before_,master_;
  std::wstring owner_;
  bool volume_owned_=false,physical_volume_owned_=false,legacy_fullscale_=false;
};
}
