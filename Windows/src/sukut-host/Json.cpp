#include "sukut-host/Json.h"

#include <cctype>
#include <cstdlib>

namespace sukut::json {
namespace {

class Reader {
 public:
  explicit Reader(std::string_view text) : text_(text) {}

  bool Read(Value& out) {
    Skip();
    if (!ReadValue(out, 0)) return false;
    Skip();
    return at_ == text_.size();
  }

 private:
  void Skip() {
    while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
  }
  bool Literal(std::string_view word) {
    if (text_.substr(at_, word.size()) != word) return false;
    at_ += word.size();
    return true;
  }
  static void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }
  bool Hex4(unsigned& code) {
    if (at_ + 4 > text_.size()) return false;
    code = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[at_++];
      code <<= 4;
      if (c >= '0' && c <= '9') code |= c - '0';
      else if (c >= 'a' && c <= 'f') code |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') code |= c - 'A' + 10;
      else return false;
    }
    return true;
  }
  bool ReadString(std::string& out) {
    if (at_ >= text_.size() || text_[at_] != '"') return false;
    ++at_;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '"') return true;
      if (c != '\\') { out += c; continue; }
      if (at_ >= text_.size()) return false;
      const char e = text_[at_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = 0;
          if (!Hex4(code)) return false;
          if (code >= 0xD800 && code <= 0xDBFF && text_.substr(at_, 2) == "\\u") {
            at_ += 2;
            unsigned low = 0;
            if (!Hex4(low)) return false;
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          AppendUtf8(out, code);
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool ReadValue(Value& out, int depth) {
    if (depth > 32 || at_ >= text_.size()) return false;
    const char c = text_[at_];
    if (c == '{') {
      out.kind = Value::Kind::Object; ++at_; Skip();
      if (at_ < text_.size() && text_[at_] == '}') { ++at_; return true; }
      while (true) {
        Skip();
        std::string key;
        if (!ReadString(key)) return false;
        Skip();
        if (at_ >= text_.size() || text_[at_++] != ':') return false;
        Skip();
        Value member;
        if (!ReadValue(member, depth + 1)) return false;
        out.members.emplace_back(std::move(key), std::move(member));
        Skip();
        if (at_ < text_.size() && text_[at_] == ',') { ++at_; continue; }
        if (at_ < text_.size() && text_[at_] == '}') { ++at_; return true; }
        return false;
      }
    }
    if (c == '[') {
      out.kind = Value::Kind::Array; ++at_; Skip();
      if (at_ < text_.size() && text_[at_] == ']') { ++at_; return true; }
      while (true) {
        Skip();
        Value item;
        if (!ReadValue(item, depth + 1)) return false;
        out.items.push_back(std::move(item));
        Skip();
        if (at_ < text_.size() && text_[at_] == ',') { ++at_; continue; }
        if (at_ < text_.size() && text_[at_] == ']') { ++at_; return true; }
        return false;
      }
    }
    if (c == '"') { out.kind = Value::Kind::String; return ReadString(out.text); }
    if (Literal("true")) { out.kind = Value::Kind::Bool; out.boolean = true; return true; }
    if (Literal("false")) { out.kind = Value::Kind::Bool; out.boolean = false; return true; }
    if (Literal("null")) { out.kind = Value::Kind::Null; return true; }
    const std::size_t start = at_;
    while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) ||
           text_[at_] == '-' || text_[at_] == '+' || text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E')) ++at_;
    if (start == at_) return false;
    out.kind = Value::Kind::Number;
    out.number = std::strtod(std::string(text_.substr(start, at_ - start)).c_str(), nullptr);
    return true;
  }

  std::string_view text_;
  std::size_t at_ = 0;
};

bool SameKey(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

}  // namespace

const Value* Value::Find(std::string_view key) const {
  for (const auto& [name, value] : members) if (SameKey(name, key)) return &value;
  return nullptr;
}
std::string Value::Str(std::string_view key, std::string fallback) const {
  const auto* v = Find(key);
  return v && v->kind == Kind::String ? v->text : fallback;
}
double Value::Num(std::string_view key, double fallback) const {
  const auto* v = Find(key);
  if (!v) return fallback;
  if (v->kind == Kind::Number) return v->number;
  if (v->kind == Kind::Bool) return v->boolean ? 1.0 : 0.0;
  return fallback;
}
bool Value::Bool(std::string_view key, bool fallback) const {
  const auto* v = Find(key);
  if (!v) return fallback;
  if (v->kind == Kind::Bool) return v->boolean;
  if (v->kind == Kind::Number) return v->number != 0.0;
  return fallback;
}

std::optional<Value> Parse(std::string_view text) {
  // Tolerate a UTF-8 byte-order mark written by some editors.
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
  Value value;
  if (!Reader(text).Read(value)) return std::nullopt;
  return value;
}

std::string Escape(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size() + 8);
  for (const char c : utf8) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          static const char* hex = "0123456789abcdef";
          out += "\\u00"; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF];
        } else out += c;
    }
  }
  return out;
}

}  // namespace sukut::json
