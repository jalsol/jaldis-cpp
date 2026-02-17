#pragma once

#include <unistd.h>
#include <utility>

// tl;dr:
// - file descriptor can close itself using RAII
// - cannot copy, only move (similar to std::unique_ptr)
class FileDescriptor {
public:
  FileDescriptor() = default;
  ~FileDescriptor() {
    if (fd_ != INVALID) {
      close(fd_);
    }
  }
  explicit FileDescriptor(int fd)
      : fd_{fd} {}

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept
      : fd_{std::exchange(other.fd_, INVALID)} {}

  FileDescriptor &operator=(FileDescriptor &&other) noexcept {
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

const struct ToFdTag {} ToFd;

auto operator|(auto fd, ToFdTag _) {
  return FileDescriptor{static_cast<int>(fd)};
}

} // namespace infix
