#pragma once
#include <filesystem>
#include <string>
#include <windows.h>

namespace cleanwave::sukut {
// An idle, separate process restores the journal if the UI/engine process dies.
// Normal Stop restores first, then signals and joins this guard.
class VolumeGuard final {
 public:
  explicit VolumeGuard(const std::filesystem::path& root);
  ~VolumeGuard();
  void Check() const;
  void Stop();
  const std::wstring& Owner() const { return owner_; }
  VolumeGuard(const VolumeGuard&)=delete;
 private:
  std::wstring owner_;
  HANDLE process_=nullptr,stop_=nullptr;
};
}
