#pragma once

#include "values.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <memory_resource>
#include <string>

#define ALWAYS_INLINE __attribute__((always_inline)) inline

namespace resp {

namespace detail {

constexpr int CountDigits(std::size_t n) noexcept;
constexpr int CountDigitsInt(int n) noexcept;
ALWAYS_INLINE void AppendInteger(std::pmr::string &buffer,
                                 std::integral auto value);

} // namespace detail

template <typename T> struct TypeSerializer;

template <typename T>
concept Serializable = requires(const T &value, std::pmr::string &buffer) {
  { TypeSerializer<T>::CalculateSize(value) } -> std::same_as<std::size_t>;
  { TypeSerializer<T>::SerializeTo(buffer, value) } -> std::same_as<void>;
};

// --- String ---
template <> struct TypeSerializer<String> {
  static ALWAYS_INLINE std::size_t CalculateSize(const String &value) noexcept {
    return 1 + value.value.size() + 2; // +data\r\n
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const String &value) {
    buffer += '+';
    buffer += value.value;
    buffer += "\r\n";
  }
};
static_assert(Serializable<String>);

// --- Error ---
template <> struct TypeSerializer<Error> {
  static ALWAYS_INLINE std::size_t CalculateSize(const Error &value) noexcept {
    return 1 + value.value.size() + 2; // -data\r\n
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const Error &value) {
    buffer += '-';
    buffer += value.value;
    buffer += "\r\n";
  }
};
static_assert(Serializable<Error>);

// --- Int ---
template <> struct TypeSerializer<Int> {
  static ALWAYS_INLINE std::size_t CalculateSize(const Int &value) noexcept {
    return 1 + detail::CountDigitsInt(value.value) + 2; // :123\r\n
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const Int &value) {
    buffer += ':';
    detail::AppendInteger(buffer, value.value);
    buffer += "\r\n";
  }
};
static_assert(Serializable<Int>);

// --- BulkString ---
template <> struct TypeSerializer<BulkString> {
  static ALWAYS_INLINE std::size_t
  CalculateSize(const BulkString &value) noexcept {
    const std::size_t len = value.value.size();
    return 1 + detail::CountDigits(len) + 2 + len + 2; // $5\r\ndata\r\n
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const BulkString &value) {
    const std::size_t len = value.value.size();

    buffer += '$';
    detail::AppendInteger(buffer, len);
    buffer += "\r\n";
    buffer += value.value;
    buffer += "\r\n";
  }
};
static_assert(Serializable<BulkString>);

// --- Null ---
template <> struct TypeSerializer<Null> {
  static ALWAYS_INLINE std::size_t CalculateSize(const Null &) noexcept {
    return 5; // $-1\r\n
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const Null &) {
    buffer += "$-1\r\n";
  }
};
static_assert(Serializable<Null>);

// --- Type Variant (dispatches to per-type serializers) ---
template <> struct TypeSerializer<Type> {
  static ALWAYS_INLINE std::size_t CalculateSize(const Type &value) noexcept {
    return std::visit(
      []<typename T>(const T &v) noexcept -> std::size_t {
        return TypeSerializer<T>::CalculateSize(v);
      },
      value);
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const Type &value) {
    std::visit([&buffer]<typename T>(
                 const T &v) { TypeSerializer<T>::SerializeTo(buffer, v); },
               value);
  }
};
static_assert(Serializable<Type>);

// --- Array ---
template <> struct TypeSerializer<Array> {
  static ALWAYS_INLINE std::size_t CalculateSize(const Array &value) noexcept {
    const std::size_t count = value.value.size();
    std::size_t size = 1 + detail::CountDigits(count) + 2; // *3\r\n

    for (const auto &elem : value.value) {
      size += TypeSerializer<Type>::CalculateSize(elem);
    }

    return size;
  }

  static ALWAYS_INLINE void SerializeTo(std::pmr::string &buffer,
                                        const Array &value) {
    const std::size_t count = value.value.size();

    buffer += '*';
    detail::AppendInteger(buffer, count);
    buffer += "\r\n";

    for (const auto &elem : value.value) {
      TypeSerializer<Type>::SerializeTo(buffer, elem);
    }
  }
};
static_assert(Serializable<Array>);

class Serializer {
public:
  ALWAYS_INLINE explicit Serializer(
    std::pmr::memory_resource *arena = std::pmr::get_default_resource())
      : arena_(arena)
      , buffer_(arena) {
    buffer_.reserve(256);
  }

  ALWAYS_INLINE std::string_view Serialize(const Serializable auto &value) {
    using T = std::decay_t<decltype(value)>;
    buffer_.clear();

    const std::size_t size = TypeSerializer<T>::CalculateSize(value);
    buffer_.reserve(size);

    TypeSerializer<T>::SerializeTo(buffer_, value);
    return buffer_;
  }

  ALWAYS_INLINE std::pmr::string Take() noexcept { return std::move(buffer_); }
  ALWAYS_INLINE void Clear() noexcept { buffer_.clear(); }

private:
  std::pmr::memory_resource *arena_;
  std::pmr::string buffer_;
};

// Internal Helper
namespace detail {

constexpr int CountDigits(std::size_t n) noexcept {
  if (n == 0) {
    return 1;
  }

  int digits = ((std::bit_width(n) - 1) * 1233 >> 12) + 1;

  constexpr std::array<std::size_t, 20> powersOf10 = {0,
                                                      10,
                                                      100,
                                                      1000,
                                                      10000,
                                                      100000,
                                                      1000000,
                                                      10000000,
                                                      100000000,
                                                      1000000000,
                                                      10000000000,
                                                      100000000000,
                                                      1000000000000,
                                                      10000000000000,
                                                      100000000000000,
                                                      1000000000000000,
                                                      10000000000000000,
                                                      100000000000000000,
                                                      1000000000000000000,
                                                      10000000000000000000ULL};

  return digits + static_cast<int>(n >= powersOf10[digits]);
}

constexpr int CountDigitsInt(int n) noexcept {
  if (n == 0) {
    return 1;
  }
  if (n < 0) {
    return CountDigits(static_cast<std::size_t>(-static_cast<long long>(n))) +
           1;
  }
  return CountDigits(static_cast<std::size_t>(n));
}

ALWAYS_INLINE void AppendInteger(std::pmr::string &buffer,
                                 std::integral auto value) {
  std::array<char, 20> tmp{};
  auto [ptr, ec] = std::to_chars(tmp.begin(), tmp.end(), value);
  buffer.append(tmp.data(), ptr - tmp.begin());
}

} // namespace detail

} // namespace resp

#undef ALWAYS_INLINE // Clean up macro
