#include "host/HostSettings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace sukut::host {
namespace {

std::size_t ValueStart(const std::string& json, const char* key) {
  const auto at = json.find(std::string("\"") + key + "\"");
  if (at == std::string::npos) return std::string::npos;
  const auto colon = json.find(':', at);
  return colon == std::string::npos ? colon : json.find_first_not_of(" \t\r\n", colon + 1);
}

std::optional<std::string> String(const std::string& json, const char* key) {
  const auto start = ValueStart(json, key);
  if (start == std::string::npos || json[start] != '"') return std::nullopt;
  std::string value;
  for (auto i = start + 1; i < json.size(); ++i) {
    if (json[i] == '\\' && i + 1 < json.size()) { value += json[++i]; continue; }
    if (json[i] == '"') return value;
    value += json[i];
  }
  return std::nullopt;
}

std::optional<long long> Number(const std::string& json, const char* key) {
  const auto start = ValueStart(json, key);
  if (start == std::string::npos || (json[start] != '-' && !std::isdigit(static_cast<unsigned char>(json[start])))) return std::nullopt;
  return std::strtoll(json.c_str() + start, nullptr, 10);
}

std::optional<bool> Bool(const std::string& json, const char* key) {
  const auto start = ValueStart(json, key);
  if (start == std::string::npos) return std::nullopt;
  if (json.compare(start, 4, "true") == 0) return true;
  if (json.compare(start, 5, "false") == 0) return false;
  return std::nullopt;
}

}  // namespace

std::string JsonEscape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

std::filesystem::path HostSettings::Path() {
  if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
    return std::filesystem::path(config) / "sukut" / "settings.json";
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : ".") / ".config" / "sukut" / "settings.json";
}

HostSettings HostSettings::Load() {
  HostSettings s;
  std::ifstream in(Path());
  if (!in) return s;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string json = buffer.str();
  if (auto v = String(json, "Language"); v && (*v == "en" || *v == "ar")) s.language = *v;
  if (auto v = String(json, "Theme")) s.theme = *v;
  if (auto v = String(json, "Output")) s.output = *v;
  if (auto v = String(json, "OutputDefault")) s.output_default = *v;
  if (auto v = Number(json, "SourcePid"); v && *v >= 0) s.source_pid = static_cast<std::uint32_t>(*v);
  if (auto v = Number(json, "PreferredMode")) s.preferred_mode = std::clamp(static_cast<int>(*v), 1, 3);
  if (auto v = Number(json, "Buffering")) s.buffering = std::clamp(static_cast<int>(*v), 0, 2);
  if (auto v = Number(json, "AmplificationDb")) s.amplification_db = std::clamp(static_cast<int>(*v), 0, 12);
  if (auto v = Number(json, "OptimizedBackgroundDb")) s.optimized_background_db = std::clamp(static_cast<int>(*v), -20, 10);
  if (auto v = Bool(json, "AdvancedOptimized")) s.optimized_mix.advanced = *v;
  if (auto v = Number(json, "AdvancedVoiceDb")) s.optimized_mix.voice_db = std::clamp(static_cast<int>(*v), -20, 10);
  if (auto v = Number(json, "AdvancedMusicDb")) s.optimized_mix.music_db = std::clamp(static_cast<int>(*v), -20, 10);
  s.optimized_mix.background_db = s.optimized_background_db;
  if (auto v = Number(json, "AdvancedBackgroundDb")) s.optimized_mix.background_db = std::clamp(static_cast<int>(*v), -20, 10);
  if (auto v = Bool(json, "SetupCompleted")) s.setup_completed = *v;
  if (auto v = Bool(json, "CloseToTray")) s.close_to_tray = *v;
  if (auto v = Bool(json, "TrayNoticeShown")) s.tray_notice_shown = *v;
  if (auto v = Bool(json, "StartWithSystem")) s.start_with_system = *v;
  if (auto v = Number(json, "HostSettingsVersion"); v && *v >= 0) s.host_settings_version = static_cast<std::uint64_t>(*v);
  return s;
}

bool HostSettings::Save(std::string& error) const {
  const auto path = Path();
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    const auto b = [](bool v) { return v ? "true" : "false"; };
    out << "{\n"
        << "  \"Language\": \"" << JsonEscape(language) << "\",\n"
        << "  \"Theme\": \"" << JsonEscape(theme) << "\",\n"
        << "  \"Output\": \"" << JsonEscape(output) << "\",\n"
        << "  \"OutputDefault\": \"" << JsonEscape(output_default) << "\",\n"
        << "  \"SourcePid\": " << source_pid << ",\n"
        << "  \"PreferredMode\": " << preferred_mode << ",\n"
        << "  \"HostSettingsVersion\": " << host_settings_version << ",\n"
        << "  \"Buffering\": " << buffering << ",\n"
        << "  \"AmplificationDb\": " << amplification_db << ",\n"
        << "  \"OptimizedBackgroundDb\": " << optimized_background_db << ",\n"
    << "  \"AdvancedOptimized\": " << b(optimized_mix.advanced) << ",\n"
    << "  \"AdvancedVoiceDb\": " << optimized_mix.voice_db << ",\n"
    << "  \"AdvancedMusicDb\": " << optimized_mix.music_db << ",\n"
    << "  \"AdvancedBackgroundDb\": " << optimized_mix.background_db << ",\n"
        << "  \"SetupCompleted\": " << b(setup_completed) << ",\n"
        << "  \"CloseToTray\": " << b(close_to_tray) << ",\n"
        << "  \"TrayNoticeShown\": " << b(tray_notice_shown) << ",\n"
        << "  \"StartWithSystem\": " << b(start_with_system) << "\n"
        << "}\n";
    if (!out) { error = "cannot write " + temporary; return false; }
  }
  std::filesystem::rename(temporary, path, code);
  if (code) { error = "cannot write " + path.string() + ": " + code.message(); return false; }
  return true;
}

}  // namespace sukut::host
