#pragma once

#include <chrono>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

class Storage {
public:
  using Clock = std::chrono::steady_clock;
  using String = std::string;
  using List = std::deque<std::string>;
  using Set = std::unordered_set<std::string>;
  using Value = std::variant<String, List, Set>;

  enum class Error : std::uint8_t { NotFound, WrongType };
  template <typename T> using Result = std::expected<T, Error>;

  bool Exists(std::string_view key);
  bool Erase(std::string_view key);
  std::vector<std::string_view> Keys();
  void Clear();

  // NOTE: will be instantiated explicitly since we only need to care about:
  // string, list, set
  template <typename T> Result<T *> Find(std::string_view key);
  template <typename T> Result<T *> FindOrCreate(std::string_view key);

  bool SetExpiry(std::string_view key, std::chrono::seconds ttl);
  int GetTtl(std::string_view key); // -2 = not found, -1 = no expiry
  void Sweep(std::size_t max_checks = 20);

private:
  struct Entry {
    Value value;
    std::optional<Clock::time_point> expires_at;

    bool Expired(Clock::time_point now) const {
      return expires_at && now >= *expires_at;
    }
  };

  struct TransparentHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view sv) const noexcept {
      return std::hash<std::string_view>{}(sv);
    }
  };

  std::unordered_map<std::string, Entry, TransparentHash, std::equal_to<>>
    data_;

  Entry *FindEntry(std::string_view key);
};
