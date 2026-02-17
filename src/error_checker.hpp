#pragma once

#include <string>
#include <system_error>

// Example use case:
// FileDescriptor fd = socket(...)
//                   | throw_if_errno("msg1")
//                   | throw_if_errno("msg2", 0)
//                   | to_fd

struct ErrorChecker {
  std::string msg;
  int error_value;
};

ErrorChecker ThrowIfErrno(std::string msg, int error_value = -1) {
  return {.msg = std::move(msg), .error_value = error_value};
}

namespace infix {
auto operator|(auto result, const ErrorChecker &check) {
  if (result == static_cast<decltype(result)>(check.error_value)) [[unlikely]] {
    throw std::system_error(errno, std::generic_category(), check.msg);
  } else [[likely]] {
    return result;
  }
}
} // namespace infix
