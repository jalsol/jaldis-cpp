#pragma once

#include "resp/values.hpp"
#include "storage.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

using CommandArgs = std::span<const resp::Type>;
using CommandFn = resp::Type (*)(CommandArgs, Storage &,
                                 std::pmr::memory_resource *);

struct CommandEntry {
  std::string_view name;
  CommandFn fn;
};

template <std::size_t N> struct CommandHandler {
  std::array<CommandEntry, N> entries{};

  constexpr auto add(CommandEntry e) const {
    if (std::ranges::any_of(e.name,
                            [](char c) { return c < 'A' || c > 'Z'; })) {
      throw "command names must be uppercase ASCII";
    }

    if (std::ranges::any_of(entries, [&](const auto &existing) {
          return existing.name == e.name;
        })) {
      throw "duplicate command name";
    }

    std::array<CommandEntry, N + 1> next{};
    std::ranges::copy(entries, next.begin());
    next[N] = e;
    return CommandHandler<N + 1>{next};
  }

  resp::Type Dispatch(std::string_view name, CommandArgs args, Storage &store,
                      std::pmr::memory_resource *arena) const {
    for (const auto &cmd : entries) {
      if (cmd.name == name) {
        return cmd.fn(args, store, arena);
      }
    }
    std::pmr::string msg{arena};
    msg.reserve(22 + name.size());
    msg += "ERR unknown command '";
    msg += name;
    msg += "'";
    return resp::Error{std::move(msg)};
  }
};
