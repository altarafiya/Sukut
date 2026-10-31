#include "sukut-host/Common.h"

#include <appmodel.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "sukut-host/Json.h"

namespace sukut::host {
namespace fs = std::filesystem;

std::wstring Widen(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
  return out;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
  return out;
}

Locations Locations::Discover() {
  Locations l;
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  l.host_directory = fs::path(std::wstring(buffer.data(), length)).parent_path();
  l.app_directory = fs::exists(l.host_directory / L"app" / L"SukutEngine.dll") ? l.host_directory / L"app" : l.host_directory;
  // Same rule as the window's AppLocations: walk up to the package marker.
  l.resource_root = l.host_directory;
  for (fs::path p = l.app_directory; !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / L"sukut-package.json") || fs::exists(p / L"docs" / L"route.json")) { l.resource_root = p; break; }
    if (p == p.root_path()) break;
  }
  UINT32 family_length = 0;
  l.packaged = GetCurrentPackageFamilyName(&family_length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
  l.data_root = l.resource_root;
  if (l.packaged) {
    std::wstring family(family_length, L'\0');
    GetCurrentPackageFamilyName(&family_length, family.data());
    family.resize(family_length ? family_length - 1 : 0);
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
      // ApplicationData.Current.LocalFolder for this package, + "Sukut".
      l.data_root = fs::path(local) / L"Packages" / family / L"LocalState" / L"Sukut";
      CoTaskMemFree(local);
    }
  }
  return l;
}

Preferences Preferences::Load(const fs::path& path) {
  Preferences p;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return p;
  std::stringstream text;
  text << stream.rdbuf();
  const auto parsed = json::Parse(text.str());
  if (!parsed) return p;
  const auto& v = *parsed;
  p.language = v.Str("Language", p.language);
  p.theme = v.Str("Theme", p.theme);
  p.output = v.Str("Output"); p.source = v.Str("Source"); p.capture = v.Str("Capture");
  p.source_pid = static_cast<std::uint32_t>(v.Num("SourcePid"));
  p.settings_version = static_cast<std::uint64_t>(v.Num("HostSettingsVersion"));
  p.buffering = std::clamp(static_cast<int>(v.Num("Buffering")), 0, 2);
  p.preferred_mode = std::clamp(static_cast<int>(v.Num("PreferredMode",1)),1,3);
  p.amplification_db = std::clamp(static_cast<int>(v.Num("AmplificationDb")), 0, 12);
  p.optimized_background_db = std::clamp(static_cast<int>(v.Num("OptimizedBackgroundDb")), -20, 10);
  p.setup_completed = v.Bool("SetupCompleted");
  p.close_to_tray = v.Bool("CloseToTray", true);
  p.cable_confirmed = v.Bool("CableVolumeDisabledConfirmed");
  p.volume_guide_dismissed = v.Bool("VolumeGuideDismissed");
  return p;
}

bool Preferences::Save(const fs::path& path) const {
  // Shared with the window. HostSettingsVersion protects routing from older
  // deferred window saves. Same PascalCase shape, replaced atomically.
  std::ostringstream o;
  const auto s = [](const std::string& v) { return "\"" + json::Escape(v) + "\""; };
  const auto b = [](bool v) { return v ? "true" : "false"; };
  o << "{\n  \"Language\": " << s(language) << ",\n  \"Theme\": " << s(theme) << ",\n  \"Output\": " << s(output)
    << ",\n  \"Source\": " << s(source) << ",\n  \"Capture\": " << s(capture) << ",\n  \"SourcePid\": " << source_pid
    << ",\n  \"PreferredMode\": " << preferred_mode << ",\n  \"HostSettingsVersion\": " << settings_version << ",\n  \"Buffering\": " << buffering << ",\n  \"AmplificationDb\": " << amplification_db
    << ",\n  \"OptimizedBackgroundDb\": " << optimized_background_db << ",\n  \"SetupCompleted\": " << b(setup_completed)
    << ",\n  \"CloseToTray\": " << b(close_to_tray) << ",\n  \"CableVolumeDisabledConfirmed\": " << b(cable_confirmed)
    << ",\n  \"VolumeGuideDismissed\": " << b(volume_guide_dismissed) << "\n}";
  std::error_code ignored;
  fs::create_directories(path.parent_path(), ignored);
  auto temporary = path; temporary += L".host.tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << o.str();
    if (!out) return false;
  }
  return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool UseDarkTheme(const Preferences& preferences) {
  if (preferences.theme == "dark") return true;
  if (preferences.theme == "light") return false;
  DWORD value = 1, size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return false;
  return value == 0;
}

}  // namespace sukut::host
