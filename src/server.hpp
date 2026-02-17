#pragma once

#include "file_descriptor.hpp"

#include <array>
#include <cstdint>
#include <string>

#include <arpa/inet.h>
#include <sys/epoll.h>

class Server {
public:
  // non-copyable
  Server() = default;
  ~Server() = default;
  Server(const Server &) = delete;
  Server(Server &&) = delete;
  Server &operator=(const Server &) = delete;
  Server &operator=(Server &&) = delete;

  struct Config {
    std::string address = "0.0.0.0";
    std::uint16_t port = DEFAULT_PORT;
    int backlog = SOMAXCONN;
  };

  void setup(const Config &config);
  void run();

private:
  static constexpr std::uint16_t DEFAULT_PORT = 8080;
  static constexpr std::size_t MAX_EVENTS = 1024;

  FileDescriptor server_fd_;
  FileDescriptor epoll_fd_;
  std::array<epoll_event, MAX_EVENTS> event_buffer_{};
};
