#pragma once

#include "values.hpp"

#include <concepts>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string_view>

namespace resp {

class RespHandler; // Forward declaration

enum class ParseStatus : std::uint8_t {
  NeedMore,
  Done,
  Cancelled,
};

struct ParseResult {
  ParseStatus status{};
  std::size_t consumed{};
  std::optional<Type> value;
};

// Buffer size constants
inline constexpr std::size_t SMALL_BUFFER_SIZE = 32;
inline constexpr std::size_t MEDIUM_BUFFER_SIZE = 128;
inline constexpr std::size_t LARGE_BUFFER_SIZE = 512;
inline constexpr std::size_t LENGTH_BUFFER_SIZE = 16;
inline constexpr std::size_t DEFAULT_ARRAY_CAPACITY = 8;

template <typename ParserType>
concept Parser = requires(ParserType parser, std::string_view input) {
  { parser.Feed(input) } -> std::same_as<ParseResult>;
  requires std::is_move_constructible_v<ParserType>;
  requires std::is_move_assignable_v<ParserType>;
};

class TypeDispatcher {
public:
  explicit TypeDispatcher(std::pmr::memory_resource *arena) noexcept
      : arena_(arena) {}

  static ParseResult Feed(std::string_view input) noexcept;

private:
  std::pmr::memory_resource *arena_;
};

class IntParser {
public:
  explicit IntParser(std::pmr::memory_resource *arena)
      : arena_(arena) {
    buffer_.reserve(SMALL_BUFFER_SIZE);
  }

  ParseResult Feed(std::string_view input);

private:
  std::pmr::memory_resource *arena_;
  std::pmr::string buffer_{arena_};
};

static_assert(Parser<IntParser>);

template <typename ValueType> class SimpleLineParser {
public:
  explicit SimpleLineParser(std::pmr::memory_resource *arena)
      : arena_(arena) {
    buffer_.reserve(SMALL_BUFFER_SIZE);
  }

  ParseResult Feed(std::string_view input) {
    const auto crlf_pos = input.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      buffer_.append(input);
      return {.status = ParseStatus::NeedMore,
              .consumed = input.size(),
              .value = std::nullopt};
    }

    buffer_.append(input.substr(0, crlf_pos));
    const std::size_t consumed = crlf_pos + 2;

    return {.status = ParseStatus::Done,
            .consumed = consumed,
            .value = Type{ValueType{std::move(buffer_)}}};
  }

private:
  std::pmr::memory_resource *arena_;
  std::pmr::string buffer_{arena_};
};

using StringParser = SimpleLineParser<String>;
using ErrorParser = SimpleLineParser<Error>;

static_assert(Parser<StringParser>);
static_assert(Parser<ErrorParser>);

class BulkStringParser {
public:
  explicit BulkStringParser(std::pmr::memory_resource *arena)
      : arena_(arena) {
    length_buffer_.reserve(LENGTH_BUFFER_SIZE);
    data_buffer_.reserve(LARGE_BUFFER_SIZE);
  }

  ParseResult Feed(std::string_view input);

private:
  enum class State : std::uint8_t { ReadingLength, ReadingData, ReadingCRLF };

  std::pmr::memory_resource *arena_;
  std::pmr::string length_buffer_{arena_};
  std::pmr::string data_buffer_{arena_};
  State state_ = State::ReadingLength;
  int expected_length_ = -1;
};

static_assert(Parser<BulkStringParser>);

class ArrayParser {
public:
  explicit ArrayParser(std::pmr::memory_resource *arena);
  ~ArrayParser();

  ArrayParser(const ArrayParser &) = delete;
  ArrayParser &operator=(const ArrayParser &) = delete;
  ArrayParser(ArrayParser &&) noexcept;
  ArrayParser &operator=(ArrayParser &&) noexcept;
  ParseResult Feed(std::string_view input);

private:
  enum class State : std::uint8_t { ReadingLength, ReadingElements };

  std::pmr::memory_resource *arena_;
  std::pmr::string length_buffer_{arena_};
  std::pmr::vector<Type> elements_{arena_};
  RespHandler *element_handler_ = nullptr; // arena-allocated, no heap
  State state_ = State::ReadingLength;
  int expected_count_ = -1;
};

static_assert(Parser<ArrayParser>);

} // namespace resp
