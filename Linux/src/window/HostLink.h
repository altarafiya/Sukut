#pragma once

// The window's connection to sukut-host (Linux/src/host/HostServer.h): one
// SOCK_SEQPACKET connection that starts with "hello", so the host knows a
// window is open, and stays open until the window closes. Requests run in
// order on a worker thread; each reply comes back on the GTK main loop, so a
// slow mode switch never freezes the window.
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>

namespace sukut::window {

class HostLink {
 public:
  using Reply = std::function<void(bool ok, const std::string& payload)>;

  HostLink() = default;
  ~HostLink();
  HostLink(const HostLink&) = delete;
  HostLink& operator=(const HostLink&) = delete;

  // Connects and says hello, starting `host_command` (with --tray) when no
  // host answers yet. A popup snapshot skips the synchronous initial request;
  // subsequent state/catalog reads run on the worker after its first frame.
  bool Connect(const std::string& host_command, std::string& error, bool popup = false, const std::string& snapshot = {});
  const std::string& InitialState() const { return initial_state_; }
  // Queues a request; `reply` runs on the main loop.
  void Send(std::vector<std::string> fields, Reply reply = {});
  // Runs on the main loop when the host goes away (it exited or crashed).
  void OnLost(std::function<void()> lost) { lost_ = std::move(lost); }

  static std::filesystem::path SocketPath();

 private:
  void Work();
  static gboolean Hangup(gint fd, GIOCondition condition, gpointer self);

  int fd_ = -1;
  std::string initial_state_;
  guint hangup_watch_ = 0;
  std::function<void()> lost_;
  bool lost_reported_ = false;

  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::pair<std::vector<std::string>, Reply>> queue_;
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace sukut::window
