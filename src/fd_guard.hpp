#pragma once

#include <unistd.h>
#include <utility>

// tl;dr: imagine unique_ptr but for file descriptor
class FdGuard {
public:
  FdGuard() = default;
  ~FdGuard() {
    if (fd_ != INVALID) {
      close(fd_);
    }
  }
  explicit FdGuard(int fd)
      : fd_{fd} {}

  FdGuard(const FdGuard &) = delete;
  FdGuard &operator=(const FdGuard &) = delete;

  FdGuard(FdGuard &&other) noexcept
      : fd_{std::exchange(other.fd_, INVALID)} {}

  FdGuard &operator=(FdGuard &&other) noexcept {
    if (this != &other) {
      if (fd_ != INVALID) {
        close(fd_);
      }
      fd_ = std::exchange(other.fd_, INVALID);
    }
    return *this;
  }

  [[nodiscard]] int operator*() const noexcept { return fd_; }

private:
  static constexpr int INVALID = -1;
  int fd_ = INVALID;
};

namespace infix {

const struct ToFdGuardTag {
} ToFdGuard;

auto operator|(auto fd, ToFdGuardTag _) { return FdGuard{static_cast<int>(fd)}; }

} // namespace infix
