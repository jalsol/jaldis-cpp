#pragma once

#include <string>
#include <system_error>

// Example use case:
// FdGuard fd = socket(...)
//            | ThrowIfErrno("msg1")
//            | ThrowIfErrno("msg2", 0)
//            | ToFdGuard

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
