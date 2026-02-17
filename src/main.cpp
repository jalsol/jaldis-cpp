#include "server.hpp"

#include <print>

int main() {
  Server server;
  Server::Config config{
    .address = "127.0.0.1",
    .port = 8080,
  };

  std::println("Starting server on {}:{}", config.address, config.port);

  server.Setup(config);
  std::println("Server setup complete. Listening for connections...");

  server.Run();

  return 0;
}
