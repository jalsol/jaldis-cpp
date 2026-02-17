#include "server.hpp"

#include "error_checker.hpp"

#include <fcntl.h>
#include <sys/socket.h>

void Server::setup(const Config &config) {
  using namespace infix;

  // set up server_fd_
  {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0)
      | throw_if_errno("Server::server_fd_")
      | to_fd;

    const int opt = 1;
    setsockopt(*server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
      | throw_if_errno("Server setsockopt");

    sockaddr_in address = {.sin_family = AF_INET,
                           .sin_port = htons(config.port)};

    inet_pton(AF_INET, config.address.c_str(), &address.sin_addr)
      | throw_if_errno("Server inet_pton system")
      | throw_if_errno("Server inet_pton invalid IP", 0);

    bind(*server_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address))
      | throw_if_errno("Server bind");

    listen(*server_fd_, config.backlog)
      | throw_if_errno("Server listen");

    const auto cur_server_fd_flags = fcntl(*server_fd_, F_GETFL, 0)
      | throw_if_errno("Server fcntl GETFL");

    fcntl(*server_fd_, F_SETFL, cur_server_fd_flags | O_NONBLOCK)
      | throw_if_errno("Server fcntl SETFL");
  }

  // set up epoll_fd_
  {
    epoll_fd_ = epoll_create1(0)
      | throw_if_errno("Server::epoll_fd_")
      | to_fd;

    epoll_event event = {
      .data = {.fd = *server_fd_},
      .events = EPOLLIN | EPOLLET,
    };

    epoll_ctl(*epoll_fd_, EPOLL_CTL_ADD, *server_fd_, &event)
      | throw_if_errno("Server epoll_ctl");
  }
}
