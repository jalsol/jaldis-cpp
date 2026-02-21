#include "server.hpp"

#include "commands.hpp"
#include "error_checker.hpp"
#include "resp/serializer.hpp"

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

  RegisterToEpoll(*server_fd_);
}

void Server::Run() {
  while (true) {
    const auto event_count =
      epoll_wait(*epoll_fd_, event_buffer_.data(), MAX_EVENTS, -1)
        | ThrowIfErrno("Server epoll_wait");

    for (auto i = 0; i < event_count; ++i) {
      const auto &event = event_buffer_[i];

      if (event.data.fd == *server_fd_) {
        AcceptNewConnections();
      } else {
        HandleClientRequest(event.data.fd);
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

    clients_.emplace(client_fd, std::make_unique<ClientState>());
    RegisterToEpoll(client_fd);
  }
}

void Server::HandleClientRequest(int client_fd) {
  auto it = clients_.find(client_fd);
  if (it == clients_.end()) [[unlikely]] {
    CloseClient(client_fd);
    return;
  }
  auto &client = *it->second;
  std::array<char, READ_BUFFER_SIZE> buffer{};

  while (true) {
    const auto bytes_read = read(client_fd, buffer.data(), buffer.size());

    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      CloseClient(client_fd);
      return;
    }

    if (bytes_read == 0) [[unlikely]] {
      CloseClient(client_fd);
      return;
    }

    auto input =
      std::string_view{buffer.data(), static_cast<std::size_t>(bytes_read)};
    bool can_release = true;

    resp::Serializer serializer{&client.arena};
    std::pmr::string write_buf{&client.arena};
    std::size_t command_count = 0;

    while (!input.empty()) {
      auto result = client.handler.Feed(input);
      input.remove_prefix(result.consumed);

      if (!result.value.has_value()) {
        if (result.value.error() == resp::ParseStatus::Cancelled) [[unlikely]] {
          client.handler.Reset();
        } else {
          can_release = false;
        }
        break;
      }

      // Commands are RESP arrays: ["COMMAND", arg1, arg2, ...]
      const auto *arr = std::get_if<resp::Array>(&*result.value);
      if (!arr || arr->value.empty()) [[unlikely]] {
        auto response = serializer.Serialize(resp::Error{
          std::pmr::string{"ERR invalid command format", &client.arena}});
        write_buf.append(response);
        client.handler.Reset();
        continue;
      }

      const auto *name_bs = std::get_if<resp::BulkString>(arr->value.data());
      if (!name_bs) [[unlikely]] {
        auto response = serializer.Serialize(resp::Error{std::pmr::string{
          "ERR command name must be a bulk string", &client.arena}});
        write_buf.append(response);
        client.handler.Reset();
        continue;
      }

      std::span<const resp::Type> args{arr->value.data() + 1,
                                       arr->value.size() - 1};
      auto reply =
        COMMANDS.Dispatch(name_bs->value, args, store_, &client.arena);

      auto response = serializer.Serialize(reply);
      write_buf.append(response);
      ++command_count;
      client.handler.Reset();
    }

    // Periodic sweep
    commands_since_sweep_ += command_count;
    if (commands_since_sweep_ >= SWEEP_INTERVAL) [[unlikely]] {
      store_.Sweep();
      commands_since_sweep_ = 0;
    }

    // Flush all accumulated responses in a single write
    if (!write_buf.empty()) {
      if (!WriteAll(client_fd, write_buf)) [[unlikely]] {
        CloseClient(client_fd);
        return;
      }
    }

    if (can_release) {
      client.arena.release();
    }
  }
}

void Server::CloseClient(int client_fd) {
  epoll_ctl(*epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
  close(client_fd);
  clients_.erase(client_fd);
}

bool Server::WriteAll(int client_fd, std::string_view data) {
  std::size_t total_written = 0;
  while (total_written < data.size()) {
    const auto bytes_written = write(client_fd, data.data() + total_written,
                                     data.size() - total_written);
    if (bytes_written == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      return false;
    }
    total_written += bytes_written;
  }
  return true;
}

void Server::RegisterToEpoll(int fd) {
  const auto flags = fcntl(fd, F_GETFL, 0)
    | ThrowIfErrno("Server fcntl GETFL");

  fcntl(fd, F_SETFL, flags | O_NONBLOCK)
    | ThrowIfErrno("Server fcntl SETFL");

  epoll_event event = {
    .events = EPOLLIN | EPOLLET,
    .data = {.fd = fd},
  };

  epoll_ctl(*epoll_fd_, EPOLL_CTL_ADD, fd, &event)
    | ThrowIfErrno("Server epoll_ctl");
}
