#pragma once

// The phone tools on Linux (Linux/PLAN.md section 7, "Phone page"): the same
// flows as Windows (PhoneTools.cs, PhoneWorkerActivation.cs) using Debian's
// adb instead of a download. The bundled APK and activation script sit in
// <data>/android with their .sha256 files and are verified before use.
// Linux only: a phone adb lists as "no permissions" is fixed by AllowUsbAccess,
// which runs sukut-usb-access through pkexec (the desktop asks for the password).
// Every call blocks and runs adb; call them off the GTK main thread.
// Failures throw PhoneError with the English message the window translates.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sukut::window {

struct PhoneError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct PhoneDevice {
  std::string serial, state, model;  // state: device, unauthorized, offline or "no permissions"
  [[nodiscard]] std::string Label() const { return model + " · " + (serial.size() > 4 ? serial.substr(serial.size() - 4) : serial); }
};

struct PhoneInfo {
  std::string name, android, sukut_version;
  std::optional<long long> sukut_code;
  bool worker_ready = false;
};

struct SukutInstallation {
  std::optional<long long> installed_code;
  long long bundled_code = 0;
  std::string bundled_version;
  [[nodiscard]] bool CanUpdate() const { return installed_code && *installed_code < bundled_code; }
  [[nodiscard]] bool IsCurrent() const { return installed_code && *installed_code >= bundled_code; }
};

class PhoneTools {
 public:
  static constexpr const char* kPackage = "com.altarafiyah.sukut";

  // adb from PATH, or $SUKUT_ADB; the bundle from <data>/android. pkexec and
  // the installed sukut-usb-access, or $SUKUT_PKEXEC and $SUKUT_USB_HELPER (tests).
  PhoneTools();
  [[nodiscard]] bool AdbAvailable() const;

  std::vector<PhoneDevice> Devices();
  static std::optional<PhoneDevice> Select(const std::vector<PhoneDevice>& devices, const std::string& previous_serial);
  PhoneInfo Inspect(const PhoneDevice& device);
  SukutInstallation Status(const PhoneDevice& device);
  std::string Install(const PhoneDevice& device, bool update);
  std::string Open(const PhoneDevice& device);
  std::string Setup(const PhoneDevice& device);  // install or update if needed, then open
  std::string Activate(const PhoneDevice& device);
  std::string Deactivate(const PhoneDevice& device);
  std::string AllowUsbAccess();  // needs no device: the phone can't be reached yet

  // Why a listed phone can't be used yet (state isn't "device").
  static std::string NotReady(const PhoneDevice& device);

  // Parsers, shared with the Windows rules (public for the tests).
  static std::vector<PhoneDevice> ParseDevices(const std::string& text);
  static std::optional<long long> ParseVersionCode(const std::string& text);
  static bool WorkerIsReady(const std::string& state, long long version);
  static int ParseUser(const std::string& text);

 private:
  std::string Exec(const std::string& program, const std::vector<std::string>& args, int seconds, int& status);
  std::string Run(const std::vector<std::string>& args, int seconds = 20, bool allow_failure = false);
  std::string RunOn(const PhoneDevice& device, std::vector<std::string> args, int seconds = 20, bool allow_failure = false);
  void EnsureAuthorized(const PhoneDevice& device);
  int CurrentUser(const PhoneDevice& device);
  std::string WorkerState(const PhoneDevice& device, int user);
  std::filesystem::path Verified(const std::string& name, const std::string& what);

  std::string adb_, pkexec_;
  std::filesystem::path bundle_, usb_helper_;
};

}  // namespace sukut::window
