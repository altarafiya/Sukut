#pragma once

// The window's link to the host: $XDG_RUNTIME_DIR/sukut/host.socket, a
// SOCK_SEQPACKET socket (message boundaries kept, like the Windows message-mode
// pipe) in a 0700 folder; only the same user may connect (SO_PEERCRED).
// Requests are fields joined by '\x1f' (the Windows verbs); replies start with
// '1' (ok) or '0' (error).
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace sukut::host {

class HostServer {
 public:
  using Handler = std::function<std::string(const std::vector<std::string>& request)>;
  // A window said hello (connected) or went away (exit_requested: it asked Sukut to quit).
  using WindowChanged = std::function<void(bool connected, bool exit_requested)>;

  HostServer(std::filesystem::path socket_path, Handler handler, WindowChanged window_changed);
  ~HostServer();
  HostServer(const HostServer&) = delete;
  HostServer& operator=(const HostServer&) = delete;

  bool Start(std::string& error);
  void Stop();

  // Client side: one request, one reply (a second launch uses it to send "open").
  static bool Request(const std::filesystem::path& socket_path, const std::string& request, std::string& reply);
  static std::filesystem::path DefaultPath();

 private:
  void Serve();

  std::filesystem::path path_;
  Handler handler_;
  WindowChanged window_changed_;
  int listen_fd_ = -1;
  int wake_[2] = {-1, -1};
  std::thread thread_;
};

std::vector<std::string> SplitFields(const std::string& text);

}  // namespace sukut::host
