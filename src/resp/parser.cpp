#include "parser.hpp"
#include "handler.hpp"

#include <charconv>
#include <memory>

namespace resp {
namespace {

constexpr char TYPE_STRING = '+';
constexpr char TYPE_ERROR = '-';
constexpr char TYPE_INT = ':';
constexpr char TYPE_BULK_STRING = '$';
constexpr char TYPE_ARRAY = '*';
constexpr char CR = '\r';
constexpr char LF = '\n';

} // namespace

ParseResult TypeDispatcher::Feed(std::string_view input) noexcept {
  if (input.empty()) {
    return {
      .status = ParseStatus::NeedMore, .consumed = 0, .value = std::nullopt};
  }

  const char type_char = input[0];

  switch (type_char) {
  case TYPE_STRING:
  case TYPE_ERROR:
  case TYPE_INT:
  case TYPE_BULK_STRING:
  case TYPE_ARRAY:
    return {.status = ParseStatus::Done, .consumed = 1, .value = std::nullopt};
  default:
    return {
      .status = ParseStatus::Cancelled, .consumed = 0, .value = std::nullopt};
  }
}

ParseResult IntParser::Feed(std::string_view input) {
  const auto crlf_pos = input.find("\r\n");
  if (crlf_pos == std::string_view::npos) {
    buffer_.append(input);
    return {.status = ParseStatus::NeedMore,
            .consumed = input.size(),
            .value = std::nullopt};
  }

  buffer_.append(input.substr(0, crlf_pos));
  const std::size_t consumed = crlf_pos + 2;

  int value = 0;
  auto [ptr, ec] =
    std::from_chars(buffer_.data(), buffer_.data() + buffer_.size(), value);

  if (ec != std::errc{} || ptr != buffer_.data() + buffer_.size()) {
    return {.status = ParseStatus::Cancelled,
            .consumed = consumed,
            .value = std::nullopt};
  }

  return {.status = ParseStatus::Done,
          .consumed = consumed,
          .value = Type{Int{value}}};
}

ParseResult BulkStringParser::Feed(std::string_view input) {
  std::size_t consumed = 0;

  if (state_ == State::ReadingLength) {
    const auto crlf_pos = input.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      length_buffer_.append(input);
      return {.status = ParseStatus::NeedMore,
              .consumed = input.size(),
              .value = std::nullopt};
    }

    length_buffer_.append(input.substr(0, crlf_pos));
    consumed += crlf_pos + 2;
    input.remove_prefix(crlf_pos + 2);

    auto [ptr, ec] = std::from_chars(
      length_buffer_.data(), length_buffer_.data() + length_buffer_.size(),
      expected_length_);

    if (ec != std::errc{} ||
        ptr != length_buffer_.data() + length_buffer_.size()) {
      return {.status = ParseStatus::Cancelled,
              .consumed = consumed,
              .value = std::nullopt};
    }

    if (expected_length_ < 0) {
      return {.status = ParseStatus::Cancelled,
              .consumed = consumed,
              .value = std::nullopt};
    }

    state_ = State::ReadingData;
  }

  if (state_ == State::ReadingData) {
    const auto remaining =
      expected_length_ - static_cast<int>(data_buffer_.size());
    const auto to_read = std::min(remaining, static_cast<int>(input.size()));

    data_buffer_.append(input.substr(0, to_read));
    consumed += to_read;
    input.remove_prefix(to_read);

    if (static_cast<int>(data_buffer_.size()) < expected_length_) {
      return {.status = ParseStatus::NeedMore,
              .consumed = consumed,
              .value = std::nullopt};
    }

    state_ = State::ReadingCRLF;
  }

  if (state_ == State::ReadingCRLF) {
    if (input.size() < 2) {
      return {.status = ParseStatus::NeedMore,
              .consumed = consumed,
              .value = std::nullopt};
    }

    if (input[0] != CR || input[1] != LF) {
      return {.status = ParseStatus::Cancelled,
              .consumed = consumed,
              .value = std::nullopt};
    }

    return {.status = ParseStatus::Done,
            .consumed = consumed + 2,
            .value = Type{BulkString{std::move(data_buffer_)}}};
  }

  return {.status = ParseStatus::Cancelled,
          .consumed = consumed,
          .value = std::nullopt};
}

ArrayParser::ArrayParser(std::pmr::memory_resource *arena)
    : arena_(arena)
    , length_buffer_(arena)
    , elements_(arena)
    , element_handler_(std::make_unique<RespHandler>(arena)) {
  length_buffer_.reserve(LENGTH_BUFFER_SIZE);
  elements_.reserve(DEFAULT_ARRAY_CAPACITY);
}

ArrayParser::~ArrayParser() = default;

ArrayParser::ArrayParser(ArrayParser &&) noexcept = default;
ArrayParser &ArrayParser::operator=(ArrayParser &&) noexcept = default;

ParseResult ArrayParser::Feed(std::string_view input) {
  std::size_t consumed = 0;

  if (state_ == State::ReadingLength) {
    const auto crlf_pos = input.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      length_buffer_.append(input);
      return {.status = ParseStatus::NeedMore,
              .consumed = input.size(),
              .value = std::nullopt};
    }

    length_buffer_.append(input.substr(0, crlf_pos));
    consumed += crlf_pos + 2;
    input.remove_prefix(crlf_pos + 2);

    auto [ptr, ec] = std::from_chars(
      length_buffer_.data(), length_buffer_.data() + length_buffer_.size(),
      expected_count_);

    if (ec != std::errc{} ||
        ptr != length_buffer_.data() + length_buffer_.size()) {
      return {.status = ParseStatus::Cancelled,
              .consumed = consumed,
              .value = std::nullopt};
    }

    if (expected_count_ < 0) {
      return {.status = ParseStatus::Cancelled,
              .consumed = consumed,
              .value = std::nullopt};
    }

    if (expected_count_ == 0) {
      return {.status = ParseStatus::Done,
              .consumed = consumed,
              .value = Type{Array{std::pmr::vector<Type>{arena_}}}};
    }

    state_ = State::ReadingElements;
  }

  if (state_ == State::ReadingElements) {
    while (static_cast<int>(elements_.size()) < expected_count_) {
      if (input.empty()) {
        return {.status = ParseStatus::NeedMore,
                .consumed = consumed,
                .value = std::nullopt};
      }

      auto result = element_handler_->Feed(input);

      if (result.status == ParseStatus::Cancelled) {
        return {.status = ParseStatus::Cancelled,
                .consumed = consumed + result.consumed,
                .value = std::nullopt};
      }

      if (result.status == ParseStatus::NeedMore) {
        return {.status = ParseStatus::NeedMore,
                .consumed = consumed + result.consumed,
                .value = std::nullopt};
      }

      if (result.status == ParseStatus::Done && result.value) {
        elements_.push_back(std::move(*result.value));
        consumed += result.consumed;
        input.remove_prefix(result.consumed);
        element_handler_->Reset(); // Reset for next element
      }
    }

    return {.status = ParseStatus::Done,
            .consumed = consumed,
            .value = Type{Array{std::move(elements_)}}};
  }

  return {.status = ParseStatus::NeedMore,
          .consumed = consumed,
          .value = std::nullopt};
}

} // namespace resp
