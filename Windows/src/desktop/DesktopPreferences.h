#pragma once
#include "live/LiveSession.h"

namespace cleanwave {
struct DesktopPreferences {
  ProcessingMode mode = ProcessingMode::Off;
  RealtimeMix custom;
  AudioRoute route;
};
std::wstring Wide(const std::string& text);
AudioRoute ReadPreparedRoute(const std::filesystem::path& path);
DesktopPreferences LoadDesktopPreferences(const std::filesystem::path& path, AudioRoute fallback);
void SaveDesktopPreferences(const std::filesystem::path& path, const DesktopPreferences& preferences);
} // namespace cleanwave
