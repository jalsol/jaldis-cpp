#pragma once

#include "command_handler.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace detail {

inline resp::Type ErrorWrongType() {
  return resp::Error{std::pmr::string{
    "WRONGTYPE Operation against a key holding the wrong kind of value"}};
}

inline resp::Type ErrorArgCount(std::string_view cmd) {
  std::pmr::string msg;
  msg.reserve(45 + cmd.size());
  msg += "ERR wrong number of arguments for '";
  msg += cmd;
  msg += "' command";
  return resp::Error{std::move(msg)};
}

inline resp::Type ErrorNotBulkString() {
  return resp::Error{std::pmr::string{"ERR value is not a bulk string"}};
}

inline resp::Type ErrorNotInteger() {
  return resp::Error{std::pmr::string{"ERR value is not an integer"}};
}

inline resp::Type Ok() { return resp::String{std::pmr::string{"OK"}}; }

inline resp::Type Nil() { return resp::BulkString{std::pmr::string{"(nil)"}}; }

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
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("GET");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::String>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return detail::Nil();
            }
            return resp::BulkString{std::pmr::string{**result}};
          }})

    .add({.name = "SET",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("SET");
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *val = detail::AsBulkString(args[1]);
            if (!key || !val) {
              return detail::ErrorNotBulkString();
            }

            auto result =
              store.FindOrCreate<Storage::String>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType();
            }
            **result = std::string{*val};
            return detail::Ok();
          }})

    .add({.name = "DEL",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.empty()) {
              return detail::ErrorArgCount("DEL");
            }
            auto deleted = 0;
            for (const auto &arg : args) {
              const auto *key = detail::AsBulkString(arg);
              if (!key) {
                return detail::ErrorNotBulkString();
              }
              if (store.Erase(std::string_view{*key})) {
                ++deleted;
              }
            }
            return resp::Int{deleted};
          }})

    .add({.name = "PING",
          .fn = [](CommandArgs args, Storage &) -> resp::Type {
            if (args.size() > 1) {
              return detail::ErrorArgCount("PING");
            }
            if (!args.empty()) {
              const auto *msg = detail::AsBulkString(args[0]);
              if (!msg) {
                return detail::ErrorNotBulkString();
              }
              return resp::BulkString{std::pmr::string{*msg}};
            }
            return resp::String{std::pmr::string{"PONG"}};
          }})

    .add({.name = "KEYS",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (!args.empty()) {
              return detail::ErrorArgCount("KEYS");
            }
            auto keys = store.Keys();
            std::pmr::vector<resp::Type> result;
            result.reserve(keys.size());
            for (auto k : keys) {
              result.emplace_back(resp::BulkString{std::pmr::string{k}});
            }
            return resp::Array{std::move(result)};
          }})

    .add({.name = "FLUSHDB",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (!args.empty()) {
              return detail::ErrorArgCount("FLUSHDB");
            }
            store.Clear();
            return detail::Ok();
          }})

    // List operations
    .add({.name = "LPUSH",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("LPUSH");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result =
              store.FindOrCreate<Storage::List>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType();
            }
            auto *list = *result;

            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *val = detail::AsBulkString(args[i]);
              if (!val) {
                return detail::ErrorNotBulkString();
              }
              list->emplace_front(*val);
            }
            return resp::Int{static_cast<int>(list->size())};
          }})

    .add({.name = "RPUSH",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("RPUSH");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result =
              store.FindOrCreate<Storage::List>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType();
            }
            auto *list = *result;

            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *val = detail::AsBulkString(args[i]);
              if (!val) {
                return detail::ErrorNotBulkString();
              }
              list->emplace_back(*val);
            }
            return resp::Int{static_cast<int>(list->size())};
          }})

    .add({.name = "LPOP",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.empty() || args.size() > 2) {
              return detail::ErrorArgCount("LPOP");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto count = 1;
            if (args.size() == 2) {
              const auto *cnt = detail::AsBulkString(args[1]);
              if (!cnt) {
                return detail::ErrorNotBulkString();
              }
              auto parsed = detail::ParseInt(std::string_view{*cnt});
              if (!parsed || *parsed < 0) {
                return detail::ErrorNotInteger();
              }
              count = *parsed;
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return detail::Nil();
            }

            auto *list = *result;
            if (count == 1 && args.size() == 1) {
              if (list->empty()) {
                return detail::Nil();
              }
              auto val = std::move(list->front());
              list->pop_front();
              return resp::BulkString{std::pmr::string{val}};
            }

            std::pmr::vector<resp::Type> popped;
            for (auto i = 0; i < count && !list->empty(); ++i) {
              popped.emplace_back(
                resp::BulkString{std::pmr::string{list->front()}});
              list->pop_front();
            }
            return resp::Array{std::move(popped)};
          }})

    .add({.name = "RPOP",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.empty() || args.size() > 2) {
              return detail::ErrorArgCount("RPOP");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto count = 1;
            if (args.size() == 2) {
              const auto *cnt = detail::AsBulkString(args[1]);
              if (!cnt) {
                return detail::ErrorNotBulkString();
              }
              auto parsed = detail::ParseInt(std::string_view{*cnt});
              if (!parsed || *parsed < 0) {
                return detail::ErrorNotInteger();
              }
              count = *parsed;
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return detail::Nil();
            }

            auto *list = *result;
            if (count == 1 && args.size() == 1) {
              if (list->empty()) {
                return detail::Nil();
              }
              auto val = std::move(list->back());
              list->pop_back();
              return resp::BulkString{std::pmr::string{val}};
            }

            std::pmr::vector<resp::Type> popped;
            for (auto i = 0; i < count && !list->empty(); ++i) {
              popped.emplace_back(
                resp::BulkString{std::pmr::string{list->back()}});
              list->pop_back();
            }
            return resp::Array{std::move(popped)};
          }})

    .add({.name = "LLEN",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("LLEN");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Int{0};
            }
            return resp::Int{static_cast<int>((*result)->size())};
          }})

    .add({.name = "LRANGE",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 3) {
              return detail::ErrorArgCount("LRANGE");
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *start_str = detail::AsBulkString(args[1]);
            const auto *stop_str = detail::AsBulkString(args[2]);
            if (!key || !start_str || !stop_str) {
              return detail::ErrorNotBulkString();
            }

            auto start_opt = detail::ParseInt(std::string_view{*start_str});
            auto stop_opt = detail::ParseInt(std::string_view{*stop_str});
            if (!start_opt || !stop_opt) {
              return detail::ErrorNotInteger();
            }

            auto result = store.Find<Storage::List>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Array{std::pmr::vector<resp::Type>{}};
            }

            const auto *list = *result;
            const auto len = static_cast<int>(list->size());
            const auto start =
              *start_opt < 0 ? std::max(0, len + *start_opt) : *start_opt;
            const auto stop =
              std::min(*stop_opt < 0 ? len + *stop_opt : *stop_opt, len - 1);

            std::pmr::vector<resp::Type> elements;
            for (auto i = start; i <= stop; ++i) {
              elements.emplace_back(
                resp::BulkString{std::pmr::string{(*list)[i]}});
            }
            return resp::Array{std::move(elements)};
          }})

    // Set operations
    .add({.name = "SADD",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("SADD");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result =
              store.FindOrCreate<Storage::Set>(std::string_view{*key});
            if (!result) {
              return detail::ErrorWrongType();
            }
            auto *set = *result;

            auto added = 0;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *member = detail::AsBulkString(args[i]);
              if (!member) {
                return detail::ErrorNotBulkString();
              }
              if (set->insert(std::string{*member}).second) {
                ++added;
              }
            }
            return resp::Int{added};
          }})

    .add({.name = "SREM",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() < 2) {
              return detail::ErrorArgCount("SREM");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Int{0};
            }

            auto *set = *result;
            auto removed = 0;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *member = detail::AsBulkString(args[i]);
              if (!member) {
                return detail::ErrorNotBulkString();
              }
              removed += static_cast<int>(set->erase(std::string{*member}));
            }
            return resp::Int{removed};
          }})

    .add({.name = "SCARD",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("SCARD");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Int{0};
            }
            return resp::Int{static_cast<int>((*result)->size())};
          }})

    .add({.name = "SMEMBERS",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 1) {
              return detail::ErrorArgCount("SMEMBERS");
            }
            const auto *key = detail::AsBulkString(args[0]);
            if (!key) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Array{std::pmr::vector<resp::Type>{}};
            }

            std::pmr::vector<resp::Type> members;
            for (const auto &m : **result) {
              members.emplace_back(resp::BulkString{std::pmr::string{m}});
            }
            return resp::Array{std::move(members)};
          }})

    .add({.name = "SINTER",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.empty()) {
              return detail::ErrorArgCount("SINTER");
            }

            const auto *first_key = detail::AsBulkString(args[0]);
            if (!first_key) {
              return detail::ErrorNotBulkString();
            }

            auto first = store.Find<Storage::Set>(std::string_view{*first_key});
            if (!first) {
              if (first.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Array{std::pmr::vector<resp::Type>{}};
            }

            // Collect other sets
            std::vector<const Storage::Set *> others;
            for (std::size_t i = 1; i < args.size(); ++i) {
              const auto *k = detail::AsBulkString(args[i]);
              if (!k) {
                return detail::ErrorNotBulkString();
              }
              auto r = store.Find<Storage::Set>(std::string_view{*k});
              if (!r) {
                if (r.error() == Storage::Error::WrongType) {
                  return detail::ErrorWrongType();
                }
                return resp::Array{std::pmr::vector<resp::Type>{}};
              }
              others.push_back(*r);
            }

            std::pmr::vector<resp::Type> result;
            for (const auto &member : **first) {
              bool in_all = std::ranges::all_of(
                others, [&](const auto *s) { return s->contains(member); });
              if (in_all) {
                result.emplace_back(resp::BulkString{std::pmr::string{member}});
              }
            }
            return resp::Array{std::move(result)};
          }})

    .add({.name = "SISMEMBER",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("SISMEMBER");
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *member = detail::AsBulkString(args[1]);
            if (!key || !member) {
              return detail::ErrorNotBulkString();
            }

            auto result = store.Find<Storage::Set>(std::string_view{*key});
            if (!result) {
              if (result.error() == Storage::Error::WrongType) {
                return detail::ErrorWrongType();
              }
              return resp::Int{0};
            }
            return resp::Int{(*result)->contains(std::string{*member}) ? 1 : 0};
          }})

    // Expiration
    .add({.name = "EXPIRE",
          .fn = [](CommandArgs args, Storage &store) -> resp::Type {
            if (args.size() != 2) {
              return detail::ErrorArgCount("EXPIRE");
            }
            const auto *key = detail::AsBulkString(args[0]);
            const auto *secs_str = detail::AsBulkString(args[1]);
            if (!key || !secs_str) {
              return detail::ErrorNotBulkString();
            }

            auto secs = detail::ParseInt(std::string_view{*secs_str});
            if (!secs || *secs < 0) {
              return detail::ErrorNotInteger();
            }

            bool ok = store.SetExpiry(std::string_view{*key},
                                      std::chrono::seconds{*secs});
            return resp::Int{ok ? 1 : 0};
          }})

    .add(
      {.name = "TTL", .fn = [](CommandArgs args, Storage &store) -> resp::Type {
         if (args.size() != 1) {
           return detail::ErrorArgCount("TTL");
         }
         const auto *key = detail::AsBulkString(args[0]);
         if (!key) {
           return detail::ErrorNotBulkString();
         }

         return resp::Int{store.GetTtl(std::string_view{*key})};
       }});
