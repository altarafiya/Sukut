#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A small read-only JSON reader for the engine status and the shared
// preferences file. Keys match case-insensitively, like the window's
// System.Text.Json options, so "initialized" and "Language" both work.
namespace sukut::json {

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<Value> items;
  std::vector<std::pair<std::string, Value>> members;

  [[nodiscard]] const Value* Find(std::string_view key) const;
  [[nodiscard]] std::string Str(std::string_view key, std::string fallback = {}) const;
  [[nodiscard]] double Num(std::string_view key, double fallback = 0.0) const;
  [[nodiscard]] bool Bool(std::string_view key, bool fallback = false) const;
};

std::optional<Value> Parse(std::string_view text);
std::string Escape(std::string_view utf8);

}  // namespace sukut::json
