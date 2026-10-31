#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sukut::host {

std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view text);

// Where everything lives. Portable: <root>\Sukut.exe + <root>\app\...;
// MSIX: the host and window share the package root. Writable state goes to
// the package's LocalState when packaged, otherwise beside the resources.
struct Locations {
  std::filesystem::path host_directory;
  std::filesystem::path app_directory;     // SukutEngine.dll, SukutWindow.exe, Assets
  std::filesystem::path resource_root;     // sukut-package.json or docs/route.json
  std::filesystem::path data_root;         // runtime/ (preferences, journals)
  bool packaged = false;
  [[nodiscard]] std::filesystem::path Preferences() const { return data_root / L"runtime" / L"sukut-ui.json"; }
  [[nodiscard]] std::filesystem::path WindowExecutable() const { return app_directory / L"SukutWindow.exe"; }
  static Locations Discover();
};

// Same fields and JSON names the window's UiPreferences uses.
struct Preferences {
  std::string language = "en", theme = "system", output, source, capture;
  std::uint32_t source_pid = 0;
  std::uint64_t settings_version = 0;
  int buffering = 0, amplification_db = 0, optimized_background_db = 0;
  int preferred_mode = 1;
  bool setup_completed = false, close_to_tray = true, cable_confirmed = false, volume_guide_dismissed = false;
  [[nodiscard]] bool Arabic() const { return language == "ar"; }
  static Preferences Load(const std::filesystem::path& path);
  bool Save(const std::filesystem::path& path) const;
};

// True when Windows (or Sukut's own theme choice) wants dark surfaces.
bool UseDarkTheme(const Preferences& preferences);

}  // namespace sukut::host
