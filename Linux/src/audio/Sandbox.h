#pragma once

// Running inside Flatpak? (Linux/PLAN.md section 5.8.) Installed files are then
// under /app, and the app sees only its own processes and its own runtime folder.
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

namespace sukut {

inline bool InSandbox() {
  static const bool sandboxed = ::access("/.flatpak-info", F_OK) == 0;
  return sandboxed;
}

// Where the package's own files are: /usr, or /app in Flatpak.
inline std::filesystem::path InstallPrefix() { return InSandbox() ? "/app" : "/usr"; }

// Where the host's lock and socket live. Each Flatpak launch has its own
// $XDG_RUNTIME_DIR, except its app/<id> folder, which all launches share.
inline std::filesystem::path SukutRuntimeDir() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  std::filesystem::path base = runtime && *runtime ? runtime : std::filesystem::temp_directory_path();
  if (const char* id = std::getenv("FLATPAK_ID"); id && *id && InSandbox()) base = base / "app" / id;
  return base / "sukut";
}

}  // namespace sukut
