#pragma once

#include "fd_guard.hpp"
#include "resp/handler.hpp"
#include "resp/serializer.hpp"
#include "storage.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <arpa/inet.h>
#include <sys/epoll.h>

class Server {
public:
  // non-copyable, non-movable
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

  void Setup(const Config &config);
  void Run();

private:
  static constexpr std::uint16_t DEFAULT_PORT = 6379;
  static constexpr std::size_t MAX_EVENTS = 1024;
  static constexpr std::size_t READ_BUFFER_SIZE = 4096;
  static constexpr std::size_t ARENA_SIZE = 8192;

  struct ClientState {
    std::array<std::byte, ARENA_SIZE> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(),
                                              arena_buf.size()};
    resp::RespHandler handler{&arena};
    resp::Serializer serializer{&arena};
  };

  FdGuard server_fd_;
  FdGuard epoll_fd_;
  std::array<epoll_event, MAX_EVENTS> event_buffer_{};
  std::unordered_map<int, std::unique_ptr<ClientState>> clients_;
  Storage store_;

  void AcceptNewConnections();
  void HandleClientRequest(int client_fd);
  void RegisterToEpoll(int fd);
  void CloseClient(int client_fd);
  static bool WriteAll(int client_fd, std::string_view data);
};
