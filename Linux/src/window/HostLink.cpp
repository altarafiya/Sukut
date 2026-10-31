#include "window/HostLink.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace sukut::window {
namespace {

constexpr std::size_t kMaxMessage = 1 << 20;

int ConnectSocket(const std::filesystem::path& path) {
  sockaddr_un address{};
  const std::string text = path.string();
  if (text.size() >= sizeof(address.sun_path)) return -1;
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) { ::close(fd); return -1; }
  // A mode switch can take a few seconds; a host that never answers must not hang the window.
  const timeval patience{15, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof(patience));
  return fd;
}

bool Exchange(int fd, const std::string& request, std::string& reply) {
  if (::send(fd, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size())) return false;
  std::string buffer(kMaxMessage, '\0');
  const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
  if (received <= 0) return false;
  reply.assign(buffer.data(), static_cast<std::size_t>(received));
  return true;
}

std::string Join(const std::vector<std::string>& fields) {
  std::string text;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i) text += '\x1f';
    text += fields[i];
  }
  return text;
}

struct Posted {
  HostLink::Reply reply;
  bool ok;
  std::string payload;
};

}  // namespace

std::filesystem::path HostLink::SocketPath() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  const std::filesystem::path base = runtime && *runtime ? runtime : std::filesystem::temp_directory_path();
  return base / "sukut" / "host.socket";
}

HostLink::~HostLink() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);  // wakes a worker waiting for a reply
  if (worker_.joinable()) worker_.join();
  if (hangup_watch_) g_source_remove(hangup_watch_);
  if (fd_ >= 0) ::close(fd_);
}

bool HostLink::Connect(const std::string& host_command, std::string& error) {
  fd_ = ConnectSocket(SocketPath());
  if (fd_ < 0) {
    // No host yet: start it for the tray, so it doesn't open a second window.
    gchar** argv = nullptr;
    GError* failure = nullptr;
    const std::string command = host_command + " --tray";
    if (!g_shell_parse_argv(command.c_str(), nullptr, &argv, &failure) ||
        !g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &failure)) {
      error = std::string("Sukut's audio service could not start: ") + (failure ? failure->message : command);
      if (failure) g_error_free(failure);
      g_strfreev(argv);
      return false;
    }
    g_strfreev(argv);
    for (int attempt = 0; attempt < 100 && fd_ < 0; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      fd_ = ConnectSocket(SocketPath());
    }
    if (fd_ < 0) { error = "Sukut's audio service did not answer."; return false; }
  }
  std::string reply;
  if (!Exchange(fd_, "hello", reply) || reply.empty() || reply[0] != '1') {
    error = reply.size() > 1 ? reply.substr(1) : "Sukut's audio service did not answer.";
    return false;
  }
  hangup_watch_ = g_unix_fd_add(fd_, static_cast<GIOCondition>(G_IO_HUP | G_IO_ERR), Hangup, this);
  worker_ = std::thread([this] { Work(); });
  return true;
}

gboolean HostLink::Hangup(gint fd, GIOCondition, gpointer data) {
  auto* self = static_cast<HostLink*>(data);
  if (fd >= 0) self->hangup_watch_ = 0;  // the fd watch ends here; the idle report (fd -1) leaves it
  if (!self->lost_reported_ && self->lost_) { self->lost_reported_ = true; self->lost_(); }
  return G_SOURCE_REMOVE;
}

void HostLink::Send(std::vector<std::string> fields, Reply reply) {
  {
    std::lock_guard lock(mutex_);
    queue_.emplace_back(std::move(fields), std::move(reply));
  }
  ready_.notify_one();
}

void HostLink::Work() {
  while (true) {
    std::pair<std::vector<std::string>, Reply> item;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      item = std::move(queue_.front());
      queue_.pop_front();
    }
    std::string reply;
    const bool delivered = Exchange(fd_, Join(item.first), reply);
    auto* posted = new Posted{std::move(item.second), delivered && !reply.empty() && reply[0] == '1',
                              delivered ? (reply.empty() ? std::string() : reply.substr(1)) : std::string("Sukut's audio service stopped.")};
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
          auto* p = static_cast<Posted*>(data);
          if (p->reply) p->reply(p->ok, p->payload);
          return G_SOURCE_REMOVE;
        },
        posted, [](gpointer data) { delete static_cast<Posted*>(data); });
    if (!delivered) {
      // The link is out of step or gone: report it once, like a hangup.
      g_idle_add([](gpointer data) -> gboolean { return Hangup(-1, G_IO_ERR, data); }, this);
      return;
    }
  }
}

}  // namespace sukut::window
