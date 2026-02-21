#include "parser.hpp"
#include "handler.hpp"

#include <charconv>
#include <utility>

namespace resp {
namespace {

constexpr char CR = '\r';
constexpr char LF = '\n';

} // namespace

ParseResult IntParser::Feed(std::string_view input) {
  const auto crlf_pos = input.find("\r\n");
  if (crlf_pos == std::string_view::npos) {
    buffer_.append(input);
    return {.consumed = input.size(),
            .value = std::unexpected(ParseStatus::NeedMore)};
  }

  buffer_.append(input.substr(0, crlf_pos));
  const std::size_t consumed = crlf_pos + 2;

  int value = 0;
  auto [ptr, ec] =
    std::from_chars(buffer_.data(), buffer_.data() + buffer_.size(), value);

  if (ec != std::errc{} || ptr != buffer_.data() + buffer_.size()) {
    return {.consumed = consumed,
            .value = std::unexpected(ParseStatus::Cancelled)};
  }

  return {.consumed = consumed, .value = Type{Int{value}}};
}

ParseResult BulkStringParser::Feed(std::string_view input) {
  std::size_t consumed = 0;

  if (state_ == State::ReadingLength) {
    const auto crlf_pos = input.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      length_buffer_.append(input);
      return {.consumed = input.size(),
              .value = std::unexpected(ParseStatus::NeedMore)};
    }

    length_buffer_.append(input.substr(0, crlf_pos));
    consumed += crlf_pos + 2;
    input.remove_prefix(crlf_pos + 2);

    auto [ptr, ec] = std::from_chars(
      length_buffer_.data(), length_buffer_.data() + length_buffer_.size(),
      expected_length_);

    if (ec != std::errc{} ||
        ptr != length_buffer_.data() + length_buffer_.size()) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::Cancelled)};
    }

    if (expected_length_ < 0) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::Cancelled)};
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
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::NeedMore)};
    }

    state_ = State::ReadingCRLF;
  }

  if (state_ == State::ReadingCRLF) {
    if (input.size() < 2) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::NeedMore)};
    }

    if (input[0] != CR || input[1] != LF) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::Cancelled)};
    }

    return {.consumed = consumed + 2,
            .value = Type{BulkString{std::move(data_buffer_)}}};
  }

  return {.consumed = consumed,
          .value = std::unexpected(ParseStatus::Cancelled)};
}

ArrayParser::ArrayParser(std::pmr::memory_resource *arena)
    : arena_(arena)
    , length_buffer_(arena)
    , elements_(arena)
    , element_handler_(std::pmr::polymorphic_allocator<RespHandler>{arena}
                         .new_object<RespHandler>(arena)) {
  length_buffer_.reserve(LENGTH_BUFFER_SIZE);
  elements_.reserve(DEFAULT_ARRAY_CAPACITY);
}

ArrayParser::~ArrayParser() {
  if (element_handler_) {
    std::pmr::polymorphic_allocator<RespHandler>{arena_}.delete_object(
      element_handler_);
  }
}

ArrayParser::ArrayParser(ArrayParser &&other) noexcept
    : arena_(other.arena_)
    , length_buffer_(std::move(other.length_buffer_))
    , elements_(std::move(other.elements_))
    , element_handler_(std::exchange(other.element_handler_, nullptr))
    , state_(other.state_)
    , expected_count_(other.expected_count_) {}

ArrayParser &ArrayParser::operator=(ArrayParser &&other) noexcept {
  if (this != &other) {
    if (element_handler_) {
      std::pmr::polymorphic_allocator<RespHandler>{arena_}.delete_object(
        element_handler_);
    }
    arena_ = other.arena_;
    length_buffer_ = std::move(other.length_buffer_);
    elements_ = std::move(other.elements_);
    element_handler_ = std::exchange(other.element_handler_, nullptr);
    state_ = other.state_;
    expected_count_ = other.expected_count_;
  }
  return *this;
}

ParseResult ArrayParser::Feed(std::string_view input) {
  std::size_t consumed = 0;

  if (state_ == State::ReadingLength) {
    const auto crlf_pos = input.find("\r\n");
    if (crlf_pos == std::string_view::npos) {
      length_buffer_.append(input);
      return {.consumed = input.size(),
              .value = std::unexpected(ParseStatus::NeedMore)};
    }

    length_buffer_.append(input.substr(0, crlf_pos));
    consumed += crlf_pos + 2;
    input.remove_prefix(crlf_pos + 2);

    auto [ptr, ec] = std::from_chars(
      length_buffer_.data(), length_buffer_.data() + length_buffer_.size(),
      expected_count_);

    if (ec != std::errc{} ||
        ptr != length_buffer_.data() + length_buffer_.size()) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::Cancelled)};
    }

    if (expected_count_ < 0) {
      return {.consumed = consumed,
              .value = std::unexpected(ParseStatus::Cancelled)};
    }

    if (expected_count_ == 0) {
      return {.consumed = consumed,
              .value = Type{Array{std::pmr::vector<Type>{arena_}}}};
    }

    state_ = State::ReadingElements;
  }

  if (state_ == State::ReadingElements) {
    while (static_cast<int>(elements_.size()) < expected_count_) {
      if (input.empty()) {
        return {.consumed = consumed,
                .value = std::unexpected(ParseStatus::NeedMore)};
      }

      auto result = element_handler_->Feed(input);

      if (!result.value.has_value() &&
          result.value.error() == ParseStatus::Cancelled) {
        return {.consumed = consumed + result.consumed,
                .value = std::unexpected(ParseStatus::Cancelled)};
      }

      if (!result.value.has_value() &&
          result.value.error() == ParseStatus::NeedMore) {
        return {.consumed = consumed + result.consumed,
                .value = std::unexpected(ParseStatus::NeedMore)};
      }

      if (result.value.has_value()) {
        elements_.push_back(std::move(*result.value));
        consumed += result.consumed;
        input.remove_prefix(result.consumed);
        element_handler_->Reset(); // Reset for next element
      }
    }

    return {.consumed = consumed, .value = Type{Array{std::move(elements_)}}};
  }

  return {.consumed = consumed,
          .value = std::unexpected(ParseStatus::NeedMore)};
}

} // namespace resp
