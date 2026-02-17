#include <print>
#include <thread>

int main() {
  std::println("{}", std::thread::hardware_concurrency());
}
