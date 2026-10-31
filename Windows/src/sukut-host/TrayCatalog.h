#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sukut::host {
struct SourceAppChoice { std::uint32_t pid; std::wstring name; };
// Only called for an explicit flyout/catalog request, never by idle polling.
std::vector<SourceAppChoice> VisibleSourceApps();
std::vector<SourceAppChoice> MergeSourceApps(const std::vector<SourceAppChoice>& sessions,
                                          const std::vector<SourceAppChoice>& windows);
// Mirrors the main window's endpoint-name mapping, including USB headsets
// that Windows names "Speakers (HyperX...)". Unknown devices use a speaker.
const wchar_t* OutputGlyph(std::wstring_view name);
}  // namespace sukut::host
