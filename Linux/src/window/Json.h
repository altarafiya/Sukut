#pragma once

// Just enough JSON for the host's replies (HostMain.cpp): objects, arrays,
// strings, numbers and booleans. Unknown or malformed input gives null values.
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sukut::window {

struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  static Json Parse(const std::string& source) {
    std::size_t at = 0;
    return Value(source, at);
  }
  const Json& operator[](const std::string& key) const {
    static const Json none;
    const auto found = fields.find(key);
    return found == fields.end() ? none : found->second;
  }
  [[nodiscard]] std::string Str(const std::string& key) const { return (*this)[key].text; }
  [[nodiscard]] long long Int(const std::string& key) const { return static_cast<long long>((*this)[key].number); }
  [[nodiscard]] bool Bool(const std::string& key) const { return (*this)[key].boolean; }

 private:
  static void Space(const std::string& s, std::size_t& at) {
    while (at < s.size() && (s[at] == ' ' || s[at] == '\n' || s[at] == '\r' || s[at] == '\t')) ++at;
  }
  static std::string String(const std::string& s, std::size_t& at) {
    std::string out;
    for (++at; at < s.size() && s[at] != '"'; ++at) {
      if (s[at] != '\\' || at + 1 >= s.size()) { out += s[at]; continue; }
      const char escaped = s[++at];
      if (escaped == 'n') out += '\n';
      else if (escaped == 't') out += '\t';
      else if (escaped == 'u' && at + 4 < s.size()) {
        const unsigned code = static_cast<unsigned>(std::strtoul(s.substr(at + 1, 4).c_str(), nullptr, 16));
        at += 4;
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
      } else out += escaped;
    }
    ++at;
    return out;
  }
  static Json Value(const std::string& s, std::size_t& at) {
    Json v;
    Space(s, at);
    if (at >= s.size()) return v;
    const char c = s[at];
    if (c == '{') {
      v.kind = Kind::Object;
      ++at;
      while (true) {
        Space(s, at);
        if (at >= s.size() || s[at] == '}') { ++at; return v; }
        if (s[at] == ',') { ++at; continue; }
        if (s[at] != '"') return v;
        const std::string key = String(s, at);
        Space(s, at);
        if (at < s.size() && s[at] == ':') ++at;
        v.fields[key] = Value(s, at);
      }
    }
    if (c == '[') {
      v.kind = Kind::Array;
      ++at;
      while (true) {
        Space(s, at);
        if (at >= s.size() || s[at] == ']') { ++at; return v; }
        if (s[at] == ',') { ++at; continue; }
        v.items.push_back(Value(s, at));
      }
    }
    if (c == '"') { v.kind = Kind::String; v.text = String(s, at); return v; }
    if (s.compare(at, 4, "true") == 0) { v.kind = Kind::Bool; v.boolean = true; at += 4; return v; }
    if (s.compare(at, 5, "false") == 0) { v.kind = Kind::Bool; at += 5; return v; }
    if (s.compare(at, 4, "null") == 0) { at += 4; return v; }
    char* end = nullptr;
    v.number = std::strtod(s.c_str() + at, &end);
    v.kind = Kind::Number;
    at = end ? static_cast<std::size_t>(end - s.c_str()) : s.size();
    if (at == 0) at = s.size();
    return v;
  }
};

}  // namespace sukut::window
