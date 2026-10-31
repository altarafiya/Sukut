#include "audio/RoutingJournal.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sukut::linux_audio {
namespace {

std::string Escape(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

std::size_t ValueStart(const std::string& json, const std::string& key) {
  const auto at = json.find("\"" + key + "\"");
  if (at == std::string::npos) return std::string::npos;
  const auto colon = json.find(':', at);
  return colon == std::string::npos ? colon : json.find_first_not_of(" \t", colon + 1);
}

// Readers for the small JSON object written below.
std::optional<std::string> StringField(const std::string& json, const std::string& key) {
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

std::uint64_t NumberField(const std::string& json, const std::string& key) {
  const auto start = ValueStart(json, key);
  return start == std::string::npos ? 0 : std::strtoull(json.c_str() + start, nullptr, 10);
}

std::vector<std::uint32_t> NumberList(const std::string& json, const std::string& key) {
  std::vector<std::uint32_t> values;
  auto start = ValueStart(json, key);
  if (start == std::string::npos || json[start] != '[') return values;
  const auto end = json.find(']', start);
  std::stringstream list(json.substr(start + 1, end - start - 1));
  std::string item;
  while (std::getline(list, item, ','))
    if (item.find_first_of("0123456789") != std::string::npos) values.push_back(static_cast<std::uint32_t>(std::stoul(item)));
  return values;
}

}  // namespace

std::filesystem::path JournalPath() {
  if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
    return std::filesystem::path(state) / "sukut" / "routing.json";
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : ".") / ".local" / "state" / "sukut" / "routing.json";
}

bool WriteJournal(const RoutingJournal& journal, std::string& error) {
  const auto path = JournalPath();
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    out << "{\"version\":2,\"sink\":\"sukut_sink\",\"previous_sink\":\"" << Escape(journal.previous_sink)
        << "\",\"default_switched\":" << (journal.default_switched ? 1 : 0)
        << ",\"source_pid\":" << journal.source_pid << ",\"targets\":[";
    for (std::size_t i = 0; i < journal.targets.size(); ++i) out << (i ? "," : "") << journal.targets[i];
    out << "]}\n";
    if (!out) { error = "cannot write " + temporary; return false; }
  }
  std::filesystem::rename(temporary, path, code);  // atomic replace
  if (code) { error = "cannot write " + path.string() + ": " + code.message(); return false; }
  return true;
}

std::optional<RoutingJournal> ReadJournal() {
  std::ifstream in(JournalPath());
  if (!in) return std::nullopt;
  std::stringstream text;
  text << in.rdbuf();
  const std::string json = text.str();
  auto previous = StringField(json, "previous_sink");
  if (!previous) return std::nullopt;
  RoutingJournal journal;
  journal.previous_sink = *previous;
  // Version 1 journals only ever recorded a switched default.
  journal.default_switched = json.find("\"default_switched\"") == std::string::npos || NumberField(json, "default_switched") != 0;
  journal.source_pid = static_cast<std::uint32_t>(NumberField(json, "source_pid"));
  journal.targets = NumberList(json, "targets");
  return journal;
}

void RemoveJournal() {
  std::error_code ignored;
  std::filesystem::remove(JournalPath(), ignored);
}

}  // namespace sukut::linux_audio
