#include "server.hpp"

#include "error_checker.hpp"

#include <fcntl.h>
#include <sys/socket.h>

using namespace infix;

void Server::Setup(const Config &config) {
  // set up server_fd_
  {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0)
      | ThrowIfErrno("Server::server_fd_")
      | ToFdGuard;

    const int opt = 1;
    setsockopt(*server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
      | ThrowIfErrno("Server setsockopt");

    sockaddr_in address = {.sin_family = AF_INET,
                           .sin_port = htons(config.port)};

    inet_pton(AF_INET, config.address.c_str(), &address.sin_addr)
      | ThrowIfErrno("Server inet_pton system")
      | ThrowIfErrno("Server inet_pton invalid IP", 0);

    bind(*server_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address))
      | ThrowIfErrno("Server bind");

    listen(*server_fd_, config.backlog)
      | ThrowIfErrno("Server listen");
  }

  epoll_fd_ = epoll_create1(0)
    | ThrowIfErrno("Server::epoll_fd_")
    | ToFdGuard;

  RegisterToEpoll(*server_fd_, EPOLLIN | EPOLLET);
}

void Server::Run() {
  while (true) {
    const auto event_count = epoll_wait(*epoll_fd_, event_buffer_.data(), MAX_EVENTS, -1)
      | ThrowIfErrno("Server epoll_wait");

    for (auto i = 0; i < event_count; ++i) {
      const auto& event = event_buffer_[i];

      if (event.data.fd == *server_fd_) {
        AcceptNewConnections();
      } else {
        // Handle client socket events (read/write)
        // TODO: Implement client event handling
      }
    }
  }
}

void Server::AcceptNewConnections() {
  while (true) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    const auto client_fd = accept(
      *server_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        throw std::system_error(errno, std::generic_category(),
                                "Server accept");
      }
    }

    RegisterToEpoll(client_fd, EPOLLIN | EPOLLET);
  }
}

void Server::RegisterToEpoll(int fd, std::uint32_t events) {
  const auto flags = fcntl(fd, F_GETFL, 0)
    | ThrowIfErrno("Server fcntl GETFL");

  fcntl(fd, F_SETFL, flags | O_NONBLOCK)
    | ThrowIfErrno("Server fcntl SETFL");

  epoll_event event = {
    .events = events,
    .data = {.fd = fd},
  };

  epoll_ctl(*epoll_fd_, EPOLL_CTL_ADD, fd, &event)
    | ThrowIfErrno("Server epoll_ctl");
}
