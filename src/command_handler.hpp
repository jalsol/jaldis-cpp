#pragma once

#include "resp/values.hpp"
#include "storage.hpp"

#include <array>
#include <span>
#include <string_view>

using CommandArgs = std::span<const resp::Type>;
using CommandFn = resp::Type (*)(CommandArgs, Storage &);

struct CommandEntry {
  std::string_view name;
  CommandFn fn;
};

template <std::size_t N> struct CommandHandler {
  std::array<CommandEntry, N> entries{};

  constexpr auto add(CommandEntry e) const {
    for (char c : e.name) {
      if (c < 'A' || c > 'Z') {
        throw "command names must be uppercase ASCII";
      }
    }

    for (const auto &existing : entries) {
      if (existing.name == e.name) {
        throw "duplicate command name";
      }
    }

    std::array<CommandEntry, N + 1> next{};
    for (std::size_t i = 0; i < N; i++) {
      next[i] = entries[i];
    }
    next[N] = e;
    return CommandHandler<N + 1>{next};
  }

  resp::Type Dispatch(std::string_view name, CommandArgs args,
                      Storage &store) const {
    for (const auto &cmd : entries) {
      if (cmd.name == name) {
        return cmd.fn(args, store);
      }
    }
    std::pmr::string msg;
    msg.reserve(22 + name.size());
    msg += "ERR unknown command '";
    msg += name;
    msg += "'";
    return resp::Type{resp::Error{std::move(msg)}};
  }
};
