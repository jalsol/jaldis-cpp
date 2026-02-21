#include "handler.hpp"

#include <type_traits>
#include <utility>

namespace resp {
namespace {

constexpr char TYPE_STRING = '+';
constexpr char TYPE_ERROR = '-';
constexpr char TYPE_INT = ':';
constexpr char TYPE_BULK_STRING = '$';
constexpr char TYPE_ARRAY = '*';

} // namespace

ParseResult RespHandler::Feed(std::string_view input) {
  if (!std::holds_alternative<std::monostate>(parser_)) {
    return FeedParser(input);
  }

  if (input.empty()) {
    return {.consumed = 0, .value = std::unexpected(ParseStatus::NeedMore)};
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
    return {.consumed = 0, .value = std::unexpected(ParseStatus::Cancelled)};
  }

  input.remove_prefix(1);
  auto result = FeedParser(input);
  result.consumed += 1;
  return result;
}

ParseResult RespHandler::FeedParser(std::string_view input) {
  return std::visit(
    [input](auto &parser) -> ParseResult {
      if constexpr (std::same_as<std::decay_t<decltype(parser)>,
                                 std::monostate>) {
        std::unreachable();
      } else {
        return parser.Feed(input);
      }
    },
    parser_);
}

void RespHandler::Reset() noexcept { parser_.emplace<std::monostate>(); }

} // namespace resp
