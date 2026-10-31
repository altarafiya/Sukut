#include "host/HostServer.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace sukut::host {
namespace {

constexpr std::size_t kMaxMessage = 1 << 20;

bool FillAddress(const std::filesystem::path& path, sockaddr_un& address) {
  const std::string text = path.string();
  if (text.size() >= sizeof(address.sun_path)) return false;
  address = {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
  return true;
}

}  // namespace

std::vector<std::string> SplitFields(const std::string& text) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const auto at = text.find('\x1f', start);
    fields.push_back(text.substr(start, at == std::string::npos ? std::string::npos : at - start));
    if (at == std::string::npos) return fields;
    start = at + 1;
  }
}

std::filesystem::path HostServer::DefaultPath() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  const std::filesystem::path base = runtime && *runtime ? runtime : std::filesystem::temp_directory_path();
  return base / "sukut" / "host.socket";
}

HostServer::HostServer(std::filesystem::path socket_path, Handler handler, WindowChanged window_changed)
    : path_(std::move(socket_path)), handler_(std::move(handler)), window_changed_(std::move(window_changed)) {}

HostServer::~HostServer() { Stop(); }

bool HostServer::Start(std::string& error) {
  std::error_code code;
  std::filesystem::create_directories(path_.parent_path(), code);
  ::chmod(path_.parent_path().c_str(), 0700);
  sockaddr_un address{};
  if (!FillAddress(path_, address)) { error = "socket path too long"; return false; }
  ::unlink(path_.c_str());
  listen_fd_ = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0 || ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listen_fd_, 8) != 0 || ::pipe2(wake_, O_CLOEXEC) != 0) {
    error = std::string("cannot open the host socket: ") + std::strerror(errno);
    return false;
  }
  ::chmod(path_.c_str(), 0600);
  thread_ = std::thread([this] { Serve(); });
  return true;
}

void HostServer::Stop() {
  if (!thread_.joinable()) return;
  const char byte = 1;
  [[maybe_unused]] const auto written = ::write(wake_[1], &byte, 1);
  thread_.join();
  ::close(listen_fd_);
  ::close(wake_[0]);
  ::close(wake_[1]);
  ::unlink(path_.c_str());
}

void HostServer::Serve() {
  struct Client { int fd; bool window = false, exit_requested = false; };
  std::vector<Client> clients;
  std::string buffer(kMaxMessage, '\0');
  while (true) {
    std::vector<pollfd> fds{{wake_[0], POLLIN, 0}, {listen_fd_, POLLIN, 0}};
    for (const auto& client : clients) fds.push_back({client.fd, POLLIN, 0});
    if (::poll(fds.data(), fds.size(), -1) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (fds[0].revents != 0) break;
    if (fds[1].revents & POLLIN) {
      const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
      ucred peer{};
      socklen_t size = sizeof(peer);
      if (fd >= 0 && ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == ::getuid())
        clients.push_back({fd});
      else if (fd >= 0) ::close(fd);  // another user's process: refuse
    }
    for (std::size_t i = 2; i < fds.size(); ++i) {
      if (fds[i].revents == 0) continue;
      auto client = std::find_if(clients.begin(), clients.end(), [&](const Client& c) { return c.fd == fds[i].fd; });
      const auto received = ::recv(client->fd, buffer.data(), buffer.size(), 0);
      if (received <= 0) {
        if (client->window) window_changed_(false, client->exit_requested);
        ::close(client->fd);
        clients.erase(client);
        continue;
      }
      const auto fields = SplitFields(std::string(buffer.data(), static_cast<std::size_t>(received)));
      if (fields[0] == "hello" && !client->window) { client->window = true; window_changed_(true, false); }
      if (fields[0] == "exit") client->exit_requested = true;
      const std::string reply = handler_(fields);
      ::send(client->fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    }
  }
  for (const auto& client : clients) ::close(client.fd);
}

bool HostServer::Request(const std::filesystem::path& socket_path, const std::string& request, std::string& reply) {
  sockaddr_un address{};
  if (!FillAddress(socket_path, address)) return false;
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  const timeval patience{10, 0};  // a mode switch can take a few seconds; a hung host must not hang the caller
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof(patience));
  bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            ::send(fd, request.data(), request.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(request.size());
  if (ok) {
    std::string buffer(kMaxMessage, '\0');
    const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
    ok = received > 0;
    if (ok) reply.assign(buffer.data(), static_cast<std::size_t>(received));
  }
  ::close(fd);
  return ok;
}

}  // namespace sukut::host
