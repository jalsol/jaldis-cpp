#pragma once

#include "command_handler.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace detail {

inline resp::Type ErrorWrongType(std::pmr::memory_resource *arena) {
  return resp::Error{std::pmr::string{
    "WRONGTYPE Operation against a key holding the wrong kind of value",
    arena}};
}

inline resp::Type ErrorArgCount(std::string_view cmd,
                                std::pmr::memory_resource *arena) {
  std::pmr::string msg{arena};
  msg.reserve(45 + cmd.size());
  msg += "ERR wrong number of arguments for '";
  msg += cmd;
  msg += "' command";
  return resp::Error{std::move(msg)};
}

inline resp::Type ErrorNotBulkString(std::pmr::memory_resource *arena) {
  return resp::Error{std::pmr::string{"ERR value is not a bulk string", arena}};
}

inline resp::Type ErrorNotInteger(std::pmr::memory_resource *arena) {
  return resp::Error{std::pmr::string{"ERR value is not an integer", arena}};
}

inline resp::Type Ok(std::pmr::memory_resource *arena) {
  return resp::String{std::pmr::string{"OK", arena}};
}

inline resp::Type Nil(std::pmr::memory_resource *arena) {
  return resp::BulkString{std::pmr::string{"(nil)", arena}};
}

inline const std::pmr::string *AsBulkString(const resp::Type &t) {
  const auto *bs = std::get_if<resp::BulkString>(&t);
  return bs ? &bs->value : nullptr;
}

inline std::optional<int> ParseInt(std::string_view sv) {
  auto val = 0;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
    return std::nullopt;
  }
  return val;
}

} // namespace detail

// Frequency-ordered: most common commands first
inline constexpr auto COMMANDS =
  CommandHandler<0>{}
    .add({.name = "GET",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("GET", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::String>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return detail::Nil(arena);
            }
            return resp::BulkString{std::pmr::string{**result, arena}};
          }})

    .add({.name = "SET",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("SET", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *val = detail::AsBulkString(args[1]);
            if (!key || !val) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result =
              store.FindOrCreate<Storage::String>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType(arena);
            }
            **result = std::string{*val};
            return detail::Ok(arena);
          }})

    .add({.name = "DEL",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.empty()) {
              return detail::ErrorArgCount("DEL", arena);
            }
            auto deleted = 0;
            for (const auto &arg : args) {
              const auto *key = detail::AsBulkString(arg);
              if (!key) {
                return detail::ErrorNotBulkString(arena);
              }
              if (store.Erase(std::string_view{*key})) {
                ++deleted;
              }
            }
            return resp::Int{deleted};
          }})

    .add({.name = "PING",
          .fn = [](CommandArgs args, Storage &,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() > 1) {
              return detail::ErrorArgCount("PING", arena);
            }
            if (!args.empty()) {
              const auto *msg = detail::AsBulkString(args[0]);
              if (!msg) {
                return detail::ErrorNotBulkString(arena);
              }
              return resp::BulkString{std::pmr::string{*msg, arena}};
            }
            return resp::String{std::pmr::string{"PONG", arena}};
          }})

    .add({.name = "KEYS",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (!args.empty()) {
              return detail::ErrorArgCount("KEYS", arena);
            }
            auto keys = store.Keys();
            std::pmr::vector<resp::Type> result{arena};
            result.reserve(keys.size());
            for (auto k : keys) {
              result.emplace_back(resp::BulkString{std::pmr::string{k, arena}});
            }
            return resp::Array{std::move(result)};
          }})

    .add({.name = "FLUSHDB",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (!args.empty()) {
              return detail::ErrorArgCount("FLUSHDB", arena);
            }
            store.Clear();
            return detail::Ok(arena);
          }})

    // List operations
    .add({.name = "LPUSH",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("LPUSH", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result =
              store.FindOrCreate<Storage::List>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType(arena);
            }
            auto *list = *result;

            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *val = detail::AsBulkString(args[i]);
              if (!val) {
                return detail::ErrorNotBulkString(arena);
              }
              list->emplace_front(*val);
            }
            return resp::Int{static_cast<int>(list->size())};
          }})

    .add({.name = "RPUSH",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("RPUSH", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result =
              store.FindOrCreate<Storage::List>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType(arena);
            }
            auto *list = *result;

            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *val = detail::AsBulkString(args[i]);
              if (!val) {
                return detail::ErrorNotBulkString(arena);
              }
              list->emplace_back(*val);
            }
            return resp::Int{static_cast<int>(list->size())};
          }})

    .add({.name = "LPOP",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.empty() || args.size() > 2) {
              return detail::ErrorArgCount("LPOP", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto count = 1;
            if (args.size() == 2) {
              const auto *cnt = detail::AsBulkString(args[1]);
              if (!cnt) {
                return detail::ErrorNotBulkString(arena);
              }
              auto parsed = detail::ParseInt(std::string_view{*cnt});
              if (!parsed || *parsed < 0) {
                return detail::ErrorNotInteger(arena);
              }
              count = *parsed;
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return detail::Nil(arena);
            }

            auto *list = *result;
            if (count == 1 && args.size() == 1) {
              if (list->empty()) {
                return detail::Nil(arena);
              }
              auto val = std::move(list->front());
              list->pop_front();
              return resp::BulkString{std::pmr::string{val, arena}};
            }

            std::pmr::vector<resp::Type> popped{arena};
            for (auto i = 0; i < count && !list->empty(); ++i) {
              popped.emplace_back(
                resp::BulkString{std::pmr::string{list->front(), arena}});
              list->pop_front();
            }
            return resp::Array{std::move(popped)};
          }})

    .add({.name = "RPOP",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.empty() || args.size() > 2) {
              return detail::ErrorArgCount("RPOP", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto count = 1;
            if (args.size() == 2) {
              const auto *cnt = detail::AsBulkString(args[1]);
              if (!cnt) {
                return detail::ErrorNotBulkString(arena);
              }
              auto parsed = detail::ParseInt(std::string_view{*cnt});
              if (!parsed || *parsed < 0) {
                return detail::ErrorNotInteger(arena);
              }
              count = *parsed;
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return detail::Nil(arena);
            }

            auto *list = *result;
            if (count == 1 && args.size() == 1) {
              if (list->empty()) {
                return detail::Nil(arena);
              }
              auto val = std::move(list->back());
              list->pop_back();
              return resp::BulkString{std::pmr::string{val, arena}};
            }

            std::pmr::vector<resp::Type> popped{arena};
            for (auto i = 0; i < count && !list->empty(); ++i) {
              popped.emplace_back(
                resp::BulkString{std::pmr::string{list->back(), arena}});
              list->pop_back();
            }
            return resp::Array{std::move(popped)};
          }})

    .add({.name = "LLEN",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("LLEN", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Int{0};
            }
            return resp::Int{static_cast<int>((*result)->size())};
          }})

    .add({.name = "LRANGE",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 3) {
              return detail::ErrorArgCount("LRANGE", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *start_str = detail::AsBulkString(args[1]);
            const auto *stop_str = detail::AsBulkString(args[2]);
            if (!key || !start_str || !stop_str) {
              return detail::ErrorNotBulkString(arena);
            }

            auto start_opt = detail::ParseInt(std::string_view{*start_str});
            auto stop_opt = detail::ParseInt(std::string_view{*stop_str});
            if (!start_opt || !stop_opt) {
              return detail::ErrorNotInteger(arena);
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Array{std::pmr::vector<resp::Type>{arena}};
            }

            const auto *list = *result;
            const auto len = static_cast<int>(list->size());
            const auto start =
              *start_opt < 0 ? std::max(0, len + *start_opt) : *start_opt;
            const auto stop =
              std::min(*stop_opt < 0 ? len + *stop_opt : *stop_opt, len - 1);

            std::pmr::vector<resp::Type> elements{arena};
            for (auto i = start; i <= stop; ++i) {
              elements.emplace_back(
                resp::BulkString{std::pmr::string{(*list)[i], arena}});
            }
            return resp::Array{std::move(elements)};
          }})

    // Set operations
    .add({.name = "SADD",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("SADD", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result =
              store.FindOrCreate<Storage::Set>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType(arena);
            }
            auto *set = *result;

            auto added = 0;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *member = detail::AsBulkString(args[i]);
              if (!member) {
                return detail::ErrorNotBulkString(arena);
              }
              if (set->insert(std::string{*member}).second) {
                ++added;
              }
            }
            return resp::Int{added};
          }})

    .add({.name = "SREM",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("SREM", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Int{0};
            }

            auto *set = *result;
            auto removed = 0;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *member = detail::AsBulkString(args[i]);
              if (!member) {
                return detail::ErrorNotBulkString(arena);
              }
              removed += static_cast<int>(set->erase(std::string{*member}));
            }
            return resp::Int{removed};
          }})

    .add({.name = "SCARD",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("SCARD", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Int{0};
            }
            return resp::Int{static_cast<int>((*result)->size())};
          }})

    .add({.name = "SMEMBERS",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("SMEMBERS", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Array{std::pmr::vector<resp::Type>{arena}};
            }

            std::pmr::vector<resp::Type> members{arena};
            for (const auto &m : **result) {
              members.emplace_back(
                resp::BulkString{std::pmr::string{m, arena}});
            }
            return resp::Array{std::move(members)};
          }})

    .add({.name = "SINTER",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.empty()) {
              return detail::ErrorArgCount("SINTER", arena);
            }

            const auto *first_key = detail::AsBulkString(args[0]);
            if (!first_key) {
              return detail::ErrorNotBulkString(arena);
            }

            auto first = store.Find<Storage::Set>(std::string_view{*first_key});
            if (!first) {
              if (first.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Array{std::pmr::vector<resp::Type>{arena}};
            }

            // Collect other sets
            std::vector<const Storage::Set *> others;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *k = detail::AsBulkString(args[i]);
              if (!k) {
                return detail::ErrorNotBulkString(arena);
              }
              auto r = store.Find<Storage::Set>(std::string_view{*k});
              if (!r) {
                if (r.error() == Storage::Error::WrongType) {
                  return detail::ErrorWrongType(arena);
                }
                return resp::Array{std::pmr::vector<resp::Type>{arena}};
              }
              others.push_back(*r);
            }

            std::pmr::vector<resp::Type> result{arena};
            for (const auto &member : **first) {
              bool in_all = std::ranges::all_of(
                others, [&](const auto *s) { return s->contains(member); });
              if (in_all) {
                result.emplace_back(
                  resp::BulkString{std::pmr::string{member, arena}});
              }
            }
            return resp::Array{std::move(result)};
          }})

    .add({.name = "SISMEMBER",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("SISMEMBER", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *member = detail::AsBulkString(args[1]);
            if (!key || !member) {
              return detail::ErrorNotBulkString(arena);
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType(arena);
              }
              return resp::Int{0};
            }
            return resp::Int{(*result)->contains(std::string{*member}) ? 1 : 0};
          }})

    // Expiration
    .add({.name = "EXPIRE",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("EXPIRE", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *secs_str = detail::AsBulkString(args[1]);
            if (!key || !secs_str) {
              return detail::ErrorNotBulkString(arena);
            }

            auto secs = detail::ParseInt(std::string_view{*secs_str});
            if (!secs || *secs < 0) {
              return detail::ErrorNotInteger(arena);
            }

            bool ok = store.SetExpiry(std::string_view{*key},
                                      std::chrono::seconds{*secs});
            return resp::Int{ok ? 1 : 0};
          }})

    .add({.name = "TTL",
          .fn = [](CommandArgs args, Storage &store,
                   std::pmr::memory_resource *arena) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("TTL", arena);
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString(arena);
            }

            return resp::Int{store.GetTtl(std::string_view{*key})};
          }});
