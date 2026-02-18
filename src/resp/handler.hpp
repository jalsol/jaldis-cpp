#pragma once

#include "parser.hpp"

#include <memory_resource>
#include <variant>

namespace resp {

class RespHandler {
public:
  explicit RespHandler(std::pmr::memory_resource *arena =
                         std::pmr::get_default_resource()) noexcept
      : arena_(arena)
      , parser_(TypeDispatcher{arena}) {}

  ParseResult Feed(std::string_view input);
  void Reset() noexcept;

private:
  std::pmr::memory_resource *arena_;
  std::variant<TypeDispatcher, IntParser, StringParser, ErrorParser,
               BulkStringParser, ArrayParser>
    parser_;

  template <Parser P> void SwitchParser() { parser_.emplace<P>(arena_); }
};

} // namespace resp
