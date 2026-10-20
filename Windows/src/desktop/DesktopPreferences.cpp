#include <windows.h>
#include <charconv>
#include <fstream>
#include <map>
#include <stdexcept>
#include "desktop/DesktopPreferences.h"

namespace cleanwave {
std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!length) throw std::runtime_error("invalid UTF-8 setting");
  std::wstring value(length, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), length);
  return value;
}
namespace {
std::string Read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("cannot read desktop settings/route");
  if (input.tellg() > 65536) throw std::runtime_error("desktop settings exceed 64 KiB");
  input.seekg(0); return {std::istreambuf_iterator<char>(input), {}};
}
std::wstring JsonId(const std::string& text, const char* name) {
  const auto key = text.find(std::string("\"") + name + "\"");
  if (key == std::string::npos) throw std::runtime_error("prepared route key is missing");
  const auto colon = text.find(':', key), first = text.find('"', colon), last = text.find('"', first + 1);
  if (colon == std::string::npos || first == std::string::npos || last == std::string::npos)
    throw std::runtime_error("invalid prepared route");
  const auto value = text.substr(first + 1, last - first - 1);
  if (value.empty() || value.find('\\') != std::string::npos) throw std::runtime_error("invalid endpoint ID");
  return Wide(value);
}
int Integer(const std::map<std::string,std::string>& values, const char* key, int low, int high) {
  const auto it = values.find(key); if (it == values.end()) throw std::runtime_error("desktop setting is missing");
  int result; const auto parsed = std::from_chars(it->second.data(), it->second.data() + it->second.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != it->second.data() + it->second.size() || result < low || result > high)
    throw std::runtime_error("desktop setting has an invalid value");
  return result;
}
std::wstring Id(const std::map<std::string,std::string>& values, const char* key) {
  const auto it = values.find(key);
  if (it == values.end() || it->second.empty() || it->second.size() > 2048) throw std::runtime_error("invalid saved endpoint ID");
  return Wide(it->second);
}
}
AudioRoute ReadPreparedRoute(const std::filesystem::path& path) {
  auto text = Read(path);
  if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);
  return {JsonId(text,"source_render_id"), JsonId(text,"capture_id"), JsonId(text,"physical_render_id")};
}
DesktopPreferences LoadDesktopPreferences(const std::filesystem::path& path, AudioRoute fallback) {
  if (!std::filesystem::exists(path)) return {ProcessingMode::Off, {}, std::move(fallback)};
  auto text = Read(path);
  if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);
  std::map<std::string,std::string> values;
  std::size_t first = 0;
  while (first < text.size()) {
    auto end = text.find('\n',first); if (end == std::string::npos) end = text.size();
    auto line = text.substr(first,end-first); first=end+1;
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.empty() || line[0]=='#') continue;
    const auto equal=line.find('=');
    if (equal==std::string::npos || !values.emplace(line.substr(0,equal),line.substr(equal+1)).second)
      throw std::runtime_error("malformed/duplicate desktop setting");
  }
  Integer(values,"version",1,1);
  DesktopPreferences result;
  result.mode = static_cast<ProcessingMode>(Integer(values,"mode",0,2));
  result.custom = {Integer(values,"voice_db",kSourceMixMinDb,kSourceMixMaxDb), Integer(values,"music_db",kSourceMixMinDb,kSourceMixMaxDb),
                   Integer(values,"background_db",kSourceMixMinDb,kSourceMixMaxDb), Integer(values,"reduction_percent",0,100), Integer(values,"boost_db",0,6)};
  result.route = {Id(values,"source_render_id"),Id(values,"capture_id"),Id(values,"physical_render_id")};
  return result;
}
void SaveDesktopPreferences(const std::filesystem::path& path, const DesktopPreferences& p) {
  // Validate before writing; keep malformed files intact until an explicit change.
  if (p.mode < ProcessingMode::Off || p.mode > ProcessingMode::Custom ||
      p.custom.voice_db < kSourceMixMinDb || p.custom.voice_db > kSourceMixMaxDb ||
      p.custom.music_db < kSourceMixMinDb || p.custom.music_db > kSourceMixMaxDb ||
      p.custom.other_db < kSourceMixMinDb || p.custom.other_db > kSourceMixMaxDb || p.custom.reduction_percent < 0 || p.custom.reduction_percent > 100 ||
      p.custom.boost_db < 0 || p.custom.boost_db > 6) throw std::runtime_error("invalid desktop preferences");
  for (const auto& id : {p.route.source_render_id,p.route.capture_id,p.route.physical_render_id})
    if (id.empty() || id.find_first_of(L"\r\n")!=std::wstring::npos) throw std::runtime_error("invalid desktop endpoint ID");
  std::filesystem::create_directories(path.parent_path());
  auto temporary=path; temporary+=L".tmp";
  {
    std::ofstream out(temporary,std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot save desktop settings");
    out << "# CleanWave desktop preferences; source-app routing is managed separately.\nversion=1\nmode=" << static_cast<int>(p.mode)
        << "\nvoice_db=" << p.custom.voice_db << "\nmusic_db=" << p.custom.music_db << "\nbackground_db=" << p.custom.other_db
        << "\nreduction_percent=" << p.custom.reduction_percent << "\nboost_db=" << p.custom.boost_db
        << "\nsource_render_id=" << Utf8(p.route.source_render_id) << "\ncapture_id=" << Utf8(p.route.capture_id)
        << "\nphysical_render_id=" << Utf8(p.route.physical_render_id) << '\n';
    out.flush(); if (!out) throw std::runtime_error("desktop settings write failed");
  }
  for (unsigned i=0;i<10;++i) {
    if (MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return;
    const auto error=GetLastError(); if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_ACCESS_DENIED) break;
    Sleep(10);
  }
  throw std::runtime_error("desktop settings replacement failed");
}
} // namespace cleanwave
