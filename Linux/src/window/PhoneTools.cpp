#include "window/PhoneTools.h"

#include "audio/Sandbox.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

#include <glib.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "window/Style.h"

extern char** environ;

namespace sukut::window {
namespace {

constexpr std::size_t kMaxOutput = 262144;

std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string Trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string Sha256(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  GChecksum* checksum = g_checksum_new(G_CHECKSUM_SHA256);
  char buffer[65536];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) g_checksum_update(checksum, reinterpret_cast<const guchar*>(buffer), in.gcount());
  std::string hex = g_checksum_get_string(checksum);
  g_checksum_free(checksum);
  return hex;
}

// The value of one getprop line: "[key]: [value]".
std::string AndroidProperty(const std::string& properties, const std::string& key) {
  std::istringstream lines(properties);
  const std::string prefix = "[" + key + "]: [";
  for (std::string line; std::getline(lines, line);) {
    line = Trim(line);
    if (line.rfind(prefix, 0) == 0 && line.size() > prefix.size() && line.back() == ']')
      return line.substr(prefix.size(), line.size() - prefix.size() - 1);
  }
  return "";
}

bool Field(const std::string& state, const std::string& pattern) {
  return std::regex_search(state, std::regex("\\b" + pattern + "(?:[,}\\s]|$)"));
}

}  // namespace

PhoneTools::PhoneTools() {
  const auto env = [](const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return std::string(value && *value ? value : fallback);
  };
  adb_ = env("SUKUT_ADB", "adb");
  pkexec_ = env("SUKUT_PKEXEC", "pkexec");
  // The polkit action names this exact path (data/io.github.altarafiya.Sukut.policy).
  usb_helper_ = env("SUKUT_USB_HELPER", "/usr/libexec/sukut/sukut-usb-access");
  bundle_ = DataDir() / "android";
}

bool PhoneTools::AdbAvailable() const {
  if (adb_.find('/') != std::string::npos) return access(adb_.c_str(), X_OK) == 0;
  gchar* found = g_find_program_in_path(adb_.c_str());
  const bool ok = found != nullptr;
  g_free(found);
  return ok;
}

// Runs program with args and returns stdout and stderr together; status is the
// waitpid status, or -1 when the program could not be started.
std::string PhoneTools::Exec(const std::string& program, const std::vector<std::string>& args, int seconds, int& status) {
  status = -1;
  int pipe_fds[2];
  if (::pipe2(pipe_fds, O_CLOEXEC) != 0) return "";
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
  std::vector<char*> argv{const_cast<char*>(program.c_str())};
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int spawned = posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(pipe_fds[1]);
  if (spawned != 0) { ::close(pipe_fds[0]); return ""; }

  std::string output;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  bool timed_out = false, too_much = false;
  char buffer[4096];
  while (true) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) { timed_out = true; break; }
    pollfd fd{pipe_fds[0], POLLIN, 0};
    if (::poll(&fd, 1, static_cast<int>(std::min<long long>(left, 1000))) < 0 && errno != EINTR) break;
    if (!(fd.revents & (POLLIN | POLLHUP))) continue;
    const auto count = ::read(pipe_fds[0], buffer, sizeof(buffer));
    if (count <= 0) break;
    if (output.size() + static_cast<std::size_t>(count) > kMaxOutput) { too_much = true; break; }
    output.append(buffer, static_cast<std::size_t>(count));
  }
  ::close(pipe_fds[0]);
  if (timed_out || too_much) ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  if (timed_out) throw PhoneError("timeout");
  if (too_much) throw PhoneError("The command returned too much output.");
  return output;
}

std::string PhoneTools::Run(const std::vector<std::string>& args, int seconds, bool allow_failure) {
  int status = 0;
  const std::string output = Exec(adb_, args, seconds, status);
  if (status == -1) throw PhoneError("Could not start ADB. Install the adb package.");
  if (!allow_failure && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)) throw PhoneError(Trim(output));
  return output;
}

std::string PhoneTools::RunOn(const PhoneDevice& device, std::vector<std::string> args, int seconds, bool allow_failure) {
  args.insert(args.begin(), {"-s", device.serial});
  return Run(args, seconds, allow_failure);
}

std::vector<PhoneDevice> PhoneTools::ParseDevices(const std::string& text) {
  // Linux adb lists a phone it can't open as "no permissions (…)"; older
  // versions also hide its serial as question marks.
  static const std::regex line_pattern(R"(^(\S+)\s+(device|unauthorized|offline|no permissions)\b(.*)$)");
  static const std::regex serial_pattern(R"(^[A-Za-z0-9._:\-\[\]?]{1,200}$)");
  static const std::regex model_pattern(R"(\bmodel:(\S+))");
  std::vector<PhoneDevice> devices;
  std::istringstream lines(text);
  for (std::string line; std::getline(lines, line);) {
    std::smatch match;
    line = Trim(line);
    if (!std::regex_match(line, match, line_pattern) || !std::regex_match(match[1].str(), serial_pattern)) continue;
    std::smatch model;
    const std::string rest = match[3].str();
    std::string name = std::regex_search(rest, model, model_pattern) ? model[1].str() : "";
    std::replace(name.begin(), name.end(), '_', ' ');
    devices.push_back({match[1].str(), match[2].str(), name.empty() ? "Android" : name});
  }
  return devices;
}

std::vector<PhoneDevice> PhoneTools::Devices() {
  // adb can report an offline transport briefly after it starts: read again
  // without killing the shared server or changing authorisation.
  std::vector<PhoneDevice> devices;
  for (int attempt = 0; attempt < 3; ++attempt) {
    devices = ParseDevices(Run({"devices", "-l"}));
    if (std::any_of(devices.begin(), devices.end(), [](const auto& d) { return d.state != "offline"; })) break;
    if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
  }
  return devices;
}

std::optional<PhoneDevice> PhoneTools::Select(const std::vector<PhoneDevice>& devices, const std::string& previous_serial) {
  const auto previous = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.serial == previous_serial; });
  if (previous != devices.end() && previous->state == "device") return *previous;
  std::vector<PhoneDevice> online;
  std::copy_if(devices.begin(), devices.end(), std::back_inserter(online), [](const auto& d) { return d.state == "device"; });
  if (online.size() == 1) return online[0];
  if (previous != devices.end()) return *previous;
  if (devices.size() == 1) return devices[0];
  return std::nullopt;
}

std::string PhoneTools::NotReady(const PhoneDevice& device) {
  if (device.state == "unauthorized") return "Allow USB debugging on the phone, then try again.";
  if (device.state == "no permissions" && InSandbox()) return "USB access is closed to Sukut on this computer. On Android 11 or newer, open Sukut on the phone and activate it with Wireless debugging.";  // the sandbox can't install a USB rule
  if (device.state == "no permissions") return "Sukut can't access the phone yet. Choose Allow access.";
  return "The phone is offline. Reconnect the USB cable.";
}

void PhoneTools::EnsureAuthorized(const PhoneDevice& device) {
  const auto devices = Devices();
  if (std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.serial == device.serial && d.state == "device"; }))
    throw PhoneError("Unlock the phone, allow USB debugging, then refresh devices.");
}

std::optional<long long> PhoneTools::ParseVersionCode(const std::string& text) {
  std::smatch match;
  if (!std::regex_search(text, match, std::regex(R"(\bversionCode=(\d+))"))) return std::nullopt;
  return std::stoll(match[1].str());
}

bool PhoneTools::WorkerIsReady(const std::string& state, long long version) {
  return version >= 49 && Field(state, "state=READY") && Field(state, "protocol=1") && Field(state, "version=" + std::to_string(version)) &&
         Field(state, "pid=[1-9]\\d*");
}

int PhoneTools::ParseUser(const std::string& text) {
  const std::string value = Trim(text);
  if (value.empty() || value.size() > 5 || !std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
    throw PhoneError("Could not identify the current Android user.");
  const int user = std::stoi(value);
  if (user > 21474) throw PhoneError("Could not identify the current Android user.");
  return user;
}

int PhoneTools::CurrentUser(const PhoneDevice& device) { return ParseUser(RunOn(device, {"shell", "am", "get-current-user"})); }

std::string PhoneTools::WorkerState(const PhoneDevice& device, int user) {
  return RunOn(device, {"shell", "content", "call", "--user", std::to_string(user), "--uri", "content://com.altarafiyah.sukut.worker", "--method", "status"},
               15, true);
}

PhoneInfo PhoneTools::Inspect(const PhoneDevice& device) {
  EnsureAuthorized(device);
  const std::string properties = RunOn(device, {"shell", "getprop"});
  const std::string sukut = RunOn(device, {"shell", "dumpsys", "package", kPackage});
  PhoneInfo info;
  for (const char* key : {"ro.product.marketname", "ro.product.vendor.marketname", "ro.product.model"})
    if (info.name.empty()) info.name = AndroidProperty(properties, key);
  if (info.name.empty()) info.name = device.model;
  info.android = AndroidProperty(properties, "ro.build.version.release");
  std::smatch version;
  if (std::regex_search(sukut, version, std::regex(R"(\bversionName=(\d+\.\d+(?:\.\d+)?(?:-beta\.\d+)?))"))) info.sukut_version = version[1].str();
  info.sukut_code = ParseVersionCode(sukut);
  info.worker_ready = WorkerIsReady(WorkerState(device, CurrentUser(device)), info.sukut_code.value_or(0));
  return info;
}

SukutInstallation PhoneTools::Status(const PhoneDevice& device) {
  EnsureAuthorized(device);
  const std::string installed = RunOn(device, {"shell", "dumpsys", "package", kPackage});
  std::istringstream lines(ReadText(bundle_ / "Sukut.apk.version"));
  std::string version, code;
  lines >> version >> code;
  if (!std::regex_match(version, std::regex(R"(^\d+\.\d+(?:\.\d+)?(?:-beta\.\d+)?$)")) || code.empty() ||
      !std::all_of(code.begin(), code.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) || std::stoll(code) <= 0)
    throw PhoneError("Sukut APK version information is missing or invalid. Reinstall Sukut for Linux.");
  return {ParseVersionCode(installed), std::stoll(code), version};
}

std::filesystem::path PhoneTools::Verified(const std::string& name, const std::string& what) {
  const auto file = bundle_ / name;
  const std::string expected = Lower(Trim(ReadText(bundle_ / (name + ".sha256"))));
  if (!std::filesystem::exists(file) || !std::regex_match(expected, std::regex("^[a-f0-9]{64}$")) || Sha256(file) != expected)
    throw PhoneError(what + " verification failed. Reinstall Sukut for Linux.");
  return file;
}

std::string PhoneTools::Install(const PhoneDevice& device, bool update) {
  const auto status = Status(device);
  if (update && !status.installed_code) throw PhoneError("Sukut is not installed. Choose Install Sukut first.");
  if (status.installed_code && !update) return "Sukut is already installed. Your settings are kept.";
  if (status.IsCurrent()) return "Sukut is already up to date. Your settings are kept.";
  const auto apk = Verified("Sukut.apk", "Sukut APK");
  const std::string result = status.installed_code ? RunOn(device, {"install", "-r", apk.string()}, 120) : RunOn(device, {"install", apk.string()}, 120);
  if (result.find("Success") == std::string::npos) throw PhoneError(Trim(result));
  if (Status(device).installed_code != status.bundled_code)
    throw PhoneError("Sukut APK installation did not report the expected version. Check the phone and retry.");
  return status.installed_code ? "Sukut updated. Your settings are kept." : "Sukut installed. Continue to open it on the phone.";
}

std::string PhoneTools::Open(const PhoneDevice& device) {
  EnsureAuthorized(device);
  // Match a normal launcher tap: restore the task's current guide, including
  // after Android has reclaimed its process, instead of stacking a new Home.
  const std::string result = RunOn(device, {"shell", "am", "start", "-W", "-a", "android.intent.action.MAIN",
      "-c", "android.intent.category.LAUNCHER", "-f", "0x10200000", "-n", std::string(kPackage) + "/com.osama.cleanwave.MainActivity"});
  if (Lower(result).find("error:") != std::string::npos) throw PhoneError(Trim(result));
  return "Sukut opened. Activate its audio worker, then press Start on the phone.";
}

std::string PhoneTools::Setup(const PhoneDevice& device) {
  const auto status = Status(device);
  if (!status.IsCurrent()) {
    Install(device, status.installed_code.has_value());
    if (Status(device).installed_code != status.bundled_code)
      throw PhoneError("Sukut installation did not report the expected version. Check the phone and retry.");
  }
  Open(device);
  return "Sukut is installed and ready for activation.";
}

// Only a successful computer activation provisions the user-operated shortcut.
// The grant is optional: a restricted phone must still keep its ready worker.
std::string PhoneTools::FinishActivation(const PhoneDevice& device, int user, long long version, bool already_ready) {
  if (version < 58) return already_ready
      ? "Sukut audio worker is already ready. Press Start on the phone."
      : "Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart.";
  if (!WorkerIsReady(WorkerState(device, user), version))
    throw PhoneError("Sukut worker is no longer ready. Keep the phone unlocked and activate again.");
  try {
    RunOn(device, {"shell", "pm", "grant", "--user", std::to_string(user), kPackage, "android.permission.WRITE_SECURE_SETTINGS"}, 15);
    const std::string result = RunOn(device, {"shell", "content", "call", "--user", std::to_string(user),
        "--uri", "content://com.altarafiyah.sukut.worker", "--method", "computer-setup"}, 15);
    if (!Field(result, "state=COMPUTER_SETUP_READY")) throw PhoneError("Shortcut setup was not confirmed.");
    return already_ready
        ? "Sukut audio worker is already ready. Choose a mode and press Start on the phone. You can unplug USB."
        : "Sukut audio worker is ready. Choose a mode and press Start on the phone. You can unplug USB.";
  } catch (const PhoneError&) {
    if (!WorkerIsReady(WorkerState(device, user), version))
      throw PhoneError("Sukut worker is no longer ready. Keep the phone unlocked and activate again.");
    return "Sukut audio worker is ready. Developer options shortcut setup was not allowed. The worker can be used without the shortcut.";
  }
}

std::string PhoneTools::Activate(const PhoneDevice& device) {
  EnsureAuthorized(device);
  const auto installation = Status(device);
  // Activating a supported install must not try to replace Play-signed apps
  // with the differently signed bundled APK. Updates remain an explicit action.
  if (!installation.installed_code || *installation.installed_code < 49) Install(device, installation.installed_code.has_value());
  const long long version = Status(device).installed_code.value_or(0);
  if (version < 49) throw PhoneError("Update Sukut on your phone before activation.");
  const int user = CurrentUser(device);
  if (WorkerIsReady(WorkerState(device, user), version)) return FinishActivation(device, user, version, true);
  Open(device);
  const auto script = Verified("activate-worker.sh", "Sukut activation script");
  RunOn(device, {"push", script.string(), "/data/local/tmp/sukut-activate-worker.sh"});
  for (int launch = 0; launch < 2; ++launch) {
    const std::string output = RunOn(device, {"shell", "sh", "/data/local/tmp/sukut-activate-worker.sh", std::to_string(user)}, 20);
    if (output.find("SUKUT_WORKER_LAUNCHED") == std::string::npos) throw PhoneError("Could not launch Sukut worker: " + Trim(output));
    for (int attempt = 0; attempt < 12; ++attempt) {
      if (WorkerIsReady(WorkerState(device, user), version))
        return FinishActivation(device, user, version, false);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  }
  const std::string log = RunOn(device, {"shell", "tail", "-c", "6000", "/data/local/tmp/sukut-altarafiyah-worker-u" + std::to_string(user) + ".log"}, 10, true);
  throw PhoneError("Sukut worker did not connect. Keep the phone unlocked and open Sukut, then retry. " + Trim(log));
}

std::string PhoneTools::Deactivate(const PhoneDevice& device) {
  EnsureAuthorized(device);
  const int user = CurrentUser(device);
  const std::string result = RunOn(device, {"shell", "content", "call", "--user", std::to_string(user), "--uri",
                                            "content://com.altarafiyah.sukut.worker", "--method", "deactivate"});
  if (!Field(result, "state=INACTIVE")) throw PhoneError("Could not deactivate Sukut. " + Trim(result));
  return "Sukut audio worker deactivated. Activate it again before using Start.";
}

std::string PhoneTools::AllowUsbAccess() {
  if (::access(usb_helper_.c_str(), X_OK) != 0) throw PhoneError("USB access can't be fixed from this build. Install Sukut for Linux.");
  int status = 0;
  const std::string output = Exec(pkexec_, {usb_helper_.string()}, 600, status);
  if (status == -1) throw PhoneError("Could not ask for administrator permission. Reinstall Sukut for Linux.");
  // pkexec exits 126 when the request is dismissed or refused, 127 when authentication fails.
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (code == 126 || code == 127) throw PhoneError("USB access was not allowed. Choose Allow access to try again.");
  if (code != 0 || output.find("SUKUT_USB_ACCESS_READY") == std::string::npos) throw PhoneError("Could not allow USB access. " + Trim(output));
  if (output.find("group=added") != std::string::npos)
    return "USB access allowed. Unplug the phone and plug it in again. If it still isn't listed, sign out and sign in again.";
  return "USB access allowed. Unplug the phone and plug it in again.";
}

}  // namespace sukut::window
