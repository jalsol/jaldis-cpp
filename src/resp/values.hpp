#pragma once

#include <string>
#include <variant>
#include <vector>

namespace resp {

struct Type;

struct String {
  std::pmr::string value;

  String() = delete;
  explicit String(std::pmr::string v)
      : value(std::move(v)) {}
};

struct Error {
  std::pmr::string value;

  Error() = delete;
  explicit Error(std::pmr::string v)
      : value(std::move(v)) {}
};

struct Int {
  int value = 0;

  Int() = default;
  explicit Int(int v)
      : value(v) {}
};

struct BulkString {
  std::pmr::string value;

  BulkString() = delete;
  explicit BulkString(std::pmr::string v)
      : value(std::move(v)) {}
};

struct Array {
  std::pmr::vector<Type> value;

  Array() = delete;
  explicit Array(std::pmr::vector<Type> v)
      : value(std::move(v)) {}
};

struct Type : std::variant<String, Error, Int, BulkString, Array> {
  using variant::variant;
};

} // namespace resp
