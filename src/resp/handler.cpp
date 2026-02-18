#include "handler.hpp"

namespace resp {
namespace {

constexpr char TYPE_STRING = '+';
constexpr char TYPE_ERROR = '-';
constexpr char TYPE_INT = ':';
constexpr char TYPE_BULK_STRING = '$';
constexpr char TYPE_ARRAY = '*';

} // namespace

ParseResult RespHandler::Feed(std::string_view input) {
  auto result =
    std::visit([input](auto &parser) { return parser.Feed(input); }, parser_);

  if (std::holds_alternative<TypeDispatcher>(parser_) &&
      result.status == ParseStatus::Done) {
    if (input.empty()) {
      return {
        .status = ParseStatus::NeedMore, .consumed = 0, .value = std::nullopt};
    }

    const char type_char = input[0];

    switch (type_char) {
    case TYPE_STRING:
      SwitchParser<StringParser>();
      break;
    case TYPE_ERROR:
      SwitchParser<ErrorParser>();
      break;
    case TYPE_INT:
      SwitchParser<IntParser>();
      break;
    case TYPE_BULK_STRING:
      SwitchParser<BulkStringParser>();
      break;
    case TYPE_ARRAY:
      SwitchParser<ArrayParser>();
      break;
    default:
      return {
        .status = ParseStatus::Cancelled, .consumed = 0, .value = std::nullopt};
    }

    // TypeDispatcher consumed 1 byte (the type byte), skip it
    input.remove_prefix(1);
    auto next_result = Feed(input);
    next_result.consumed += 1; // Account for the type byte we skipped
    return next_result;
  }

  return result;
}

void RespHandler::Reset() noexcept { parser_.emplace<TypeDispatcher>(arena_); }

} // namespace resp
