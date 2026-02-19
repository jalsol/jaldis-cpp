#include "commands.hpp"
#include "resp/values.hpp"
#include "storage.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory_resource>

using namespace resp;

namespace {

Type bulkStr(const char *s) { return Type{BulkString{std::pmr::string{s}}}; }

const std::pmr::string &asString(const Type &t) {
  return std::get<String>(t).value;
}

const std::pmr::string &asBulk(const Type &t) {
  return std::get<BulkString>(t).value;
}

int asInt(const Type &t) { return std::get<Int>(t).value; }

const std::pmr::vector<Type> &asArray(const Type &t) {
  return std::get<Array>(t).value;
}

bool isError(const Type &t) { return std::holds_alternative<Error>(t); }

Type dispatch(Storage &store, std::initializer_list<Type> args_list,
              std::pmr::memory_resource *arena) {
  std::vector<Type> all(args_list);
  auto *name_bs = std::get_if<BulkString>(all.data());
  std::string name_upper{name_bs->value};
  std::span<const Type> args{all.data() + 1, all.size() - 1};
  return COMMANDS.Dispatch(std::string_view{name_upper}, args, store, arena);
}

} // namespace

TEST_CASE("PING command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("No args returns PONG") {
    auto result = dispatch(store, {bulkStr("PING")}, &arena);
    REQUIRE(asString(result) == "PONG");
  }

  SECTION("With message echoes it back") {
    auto result = dispatch(store, {bulkStr("PING"), bulkStr("hello")}, &arena);
    REQUIRE(asBulk(result) == "hello");
  }

  SECTION("Too many args returns error") {
    auto result =
      dispatch(store, {bulkStr("PING"), bulkStr("a"), bulkStr("b")}, &arena);
    REQUIRE(isError(result));
  }
}

TEST_CASE("SET and GET commands", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("SET then GET") {
    auto set_result = dispatch(
      store, {bulkStr("SET"), bulkStr("key"), bulkStr("value")}, &arena);
    REQUIRE(asString(set_result) == "OK");

    auto get_result = dispatch(store, {bulkStr("GET"), bulkStr("key")}, &arena);
    REQUIRE(asBulk(get_result) == "value");
  }

  SECTION("GET missing key returns nil") {
    auto result = dispatch(store, {bulkStr("GET"), bulkStr("missing")}, &arena);
    REQUIRE(asBulk(result) == "(nil)");
  }

  SECTION("SET overwrites") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("v1")}, &arena);
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("v2")}, &arena);
    auto result = dispatch(store, {bulkStr("GET"), bulkStr("key")}, &arena);
    REQUIRE(asBulk(result) == "v2");
  }

  SECTION("GET wrong arg count") {
    REQUIRE(isError(dispatch(store, {bulkStr("GET")}, &arena)));
    REQUIRE(isError(
      dispatch(store, {bulkStr("GET"), bulkStr("a"), bulkStr("b")}, &arena)));
  }

  SECTION("SET wrong arg count") {
    REQUIRE(isError(dispatch(store, {bulkStr("SET"), bulkStr("key")}, &arena)));
  }
}

TEST_CASE("DEL command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Delete existing key") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    auto result = dispatch(store, {bulkStr("DEL"), bulkStr("key")}, &arena);
    REQUIRE(asInt(result) == 1);
    REQUIRE(asBulk(dispatch(store, {bulkStr("GET"), bulkStr("key")}, &arena)) ==
            "(nil)");
  }

  SECTION("Delete missing key returns 0") {
    auto result = dispatch(store, {bulkStr("DEL"), bulkStr("missing")}, &arena);
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Delete multiple keys") {
    dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")}, &arena);
    dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")}, &arena);
    dispatch(store, {bulkStr("SET"), bulkStr("c"), bulkStr("3")}, &arena);
    auto result = dispatch(
      store, {bulkStr("DEL"), bulkStr("a"), bulkStr("b"), bulkStr("x")},
      &arena);
    REQUIRE(asInt(result) == 2);
  }
}

TEST_CASE("KEYS command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Empty store") {
    auto result = dispatch(store, {bulkStr("KEYS")}, &arena);
    REQUIRE(asArray(result).empty());
  }

  SECTION("Returns all keys") {
    dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")}, &arena);
    dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")}, &arena);
    auto result = dispatch(store, {bulkStr("KEYS")}, &arena);
    REQUIRE(asArray(result).size() == 2);
  }
}

TEST_CASE("FLUSHDB command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")}, &arena);
  dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")}, &arena);
  auto result = dispatch(store, {bulkStr("FLUSHDB")}, &arena);
  REQUIRE(asString(result) == "OK");
  REQUIRE(asArray(dispatch(store, {bulkStr("KEYS")}, &arena)).empty());
}

TEST_CASE("LPUSH and RPUSH commands", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("LPUSH creates list and returns length") {
    auto r1 = dispatch(store, {bulkStr("LPUSH"), bulkStr("list"), bulkStr("a")},
                       &arena);
    REQUIRE(asInt(r1) == 1);
    auto r2 = dispatch(store, {bulkStr("LPUSH"), bulkStr("list"), bulkStr("b")},
                       &arena);
    REQUIRE(asInt(r2) == 2);
  }

  SECTION("RPUSH creates list and returns length") {
    auto r1 = dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a")},
                       &arena);
    REQUIRE(asInt(r1) == 1);
  }

  SECTION("LPUSH multiple values") {
    auto result = dispatch(store,
                           {bulkStr("LPUSH"), bulkStr("list"), bulkStr("a"),
                            bulkStr("b"), bulkStr("c")},
                           &arena);
    REQUIRE(asInt(result) == 3);
  }

  SECTION("LPUSH on string key returns wrong type") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    auto result =
      dispatch(store, {bulkStr("LPUSH"), bulkStr("key"), bulkStr("a")}, &arena);
    REQUIRE(isError(result));
  }
}

TEST_CASE("LPOP and RPOP commands", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a")}, &arena);
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("b")}, &arena);
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("c")}, &arena);

  SECTION("LPOP returns first element") {
    auto result = dispatch(store, {bulkStr("LPOP"), bulkStr("list")}, &arena);
    REQUIRE(asBulk(result) == "a");
  }

  SECTION("RPOP returns last element") {
    auto result = dispatch(store, {bulkStr("RPOP"), bulkStr("list")}, &arena);
    REQUIRE(asBulk(result) == "c");
  }

  SECTION("LPOP with count returns array") {
    auto result =
      dispatch(store, {bulkStr("LPOP"), bulkStr("list"), bulkStr("2")}, &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "a");
    REQUIRE(asBulk(arr[1]) == "b");
  }

  SECTION("RPOP with count returns array") {
    auto result =
      dispatch(store, {bulkStr("RPOP"), bulkStr("list"), bulkStr("2")}, &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "c");
    REQUIRE(asBulk(arr[1]) == "b");
  }

  SECTION("POP from missing key returns nil") {
    auto result =
      dispatch(store, {bulkStr("LPOP"), bulkStr("missing")}, &arena);
    REQUIRE(asBulk(result) == "(nil)");
  }
}

TEST_CASE("LLEN command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Empty/missing list returns 0") {
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("list")}, &arena);
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Returns correct length") {
    dispatch(store,
             {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a"), bulkStr("b"),
              bulkStr("c")},
             &arena);
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("list")}, &arena);
    REQUIRE(asInt(result) == 3);
  }

  SECTION("Wrong type returns error") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("key")}, &arena);
    REQUIRE(isError(result));
  }
}

TEST_CASE("LRANGE command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(store,
           {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a"), bulkStr("b"),
            bulkStr("c"), bulkStr("d")},
           &arena);

  SECTION("Full range") {
    auto result = dispatch(
      store, {bulkStr("LRANGE"), bulkStr("list"), bulkStr("0"), bulkStr("-1")},
      &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 4);
    REQUIRE(asBulk(arr[0]) == "a");
    REQUIRE(asBulk(arr[3]) == "d");
  }

  SECTION("Partial range") {
    auto result = dispatch(
      store, {bulkStr("LRANGE"), bulkStr("list"), bulkStr("1"), bulkStr("2")},
      &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "b");
    REQUIRE(asBulk(arr[1]) == "c");
  }

  SECTION("Negative indices") {
    auto result = dispatch(
      store, {bulkStr("LRANGE"), bulkStr("list"), bulkStr("-2"), bulkStr("-1")},
      &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "c");
    REQUIRE(asBulk(arr[1]) == "d");
  }

  SECTION("Missing list returns empty array") {
    auto result = dispatch(
      store,
      {bulkStr("LRANGE"), bulkStr("missing"), bulkStr("0"), bulkStr("-1")},
      &arena);
    REQUIRE(asArray(result).empty());
  }
}

TEST_CASE("SADD command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Add single member") {
    auto result =
      dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Add duplicate returns 0") {
    dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")}, &arena);
    auto result =
      dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Add multiple members") {
    auto result = dispatch(store,
                           {bulkStr("SADD"), bulkStr("set"), bulkStr("a"),
                            bulkStr("b"), bulkStr("c")},
                           &arena);
    REQUIRE(asInt(result) == 3);
  }

  SECTION("Wrong type") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    REQUIRE(isError(dispatch(
      store, {bulkStr("SADD"), bulkStr("key"), bulkStr("a")}, &arena)));
  }
}

TEST_CASE("SREM command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(
    store,
    {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b"), bulkStr("c")},
    &arena);

  SECTION("Remove existing member") {
    auto result =
      dispatch(store, {bulkStr("SREM"), bulkStr("set"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Remove missing member") {
    auto result =
      dispatch(store, {bulkStr("SREM"), bulkStr("set"), bulkStr("x")}, &arena);
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Remove from missing key") {
    auto result = dispatch(
      store, {bulkStr("SREM"), bulkStr("missing"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("SCARD command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Missing set returns 0") {
    REQUIRE(
      asInt(dispatch(store, {bulkStr("SCARD"), bulkStr("set")}, &arena)) == 0);
  }

  SECTION("Returns correct size") {
    dispatch(store,
             {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b")},
             &arena);
    REQUIRE(
      asInt(dispatch(store, {bulkStr("SCARD"), bulkStr("set")}, &arena)) == 2);
  }
}

TEST_CASE("SMEMBERS command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("Missing set returns empty array") {
    auto result =
      dispatch(store, {bulkStr("SMEMBERS"), bulkStr("set")}, &arena);
    REQUIRE(asArray(result).empty());
  }

  SECTION("Returns all members") {
    dispatch(store,
             {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b")},
             &arena);
    auto result =
      dispatch(store, {bulkStr("SMEMBERS"), bulkStr("set")}, &arena);
    REQUIRE(asArray(result).size() == 2);
  }
}

TEST_CASE("SISMEMBER command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")}, &arena);

  SECTION("Member exists") {
    auto result = dispatch(
      store, {bulkStr("SISMEMBER"), bulkStr("set"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Member does not exist") {
    auto result = dispatch(
      store, {bulkStr("SISMEMBER"), bulkStr("set"), bulkStr("b")}, &arena);
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Missing key returns 0") {
    auto result = dispatch(
      store, {bulkStr("SISMEMBER"), bulkStr("missing"), bulkStr("a")}, &arena);
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("SINTER command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(
    store,
    {bulkStr("SADD"), bulkStr("s1"), bulkStr("a"), bulkStr("b"), bulkStr("c")},
    &arena);
  dispatch(
    store,
    {bulkStr("SADD"), bulkStr("s2"), bulkStr("b"), bulkStr("c"), bulkStr("d")},
    &arena);

  SECTION("Intersection of two sets") {
    auto result = dispatch(
      store, {bulkStr("SINTER"), bulkStr("s1"), bulkStr("s2")}, &arena);
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
  }

  SECTION("Intersection with missing set returns empty") {
    auto result = dispatch(
      store, {bulkStr("SINTER"), bulkStr("s1"), bulkStr("missing")}, &arena);
    REQUIRE(asArray(result).empty());
  }

  SECTION("Single set returns all members") {
    auto result = dispatch(store, {bulkStr("SINTER"), bulkStr("s1")}, &arena);
    REQUIRE(asArray(result).size() == 3);
  }
}

TEST_CASE("EXPIRE and TTL commands", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  SECTION("TTL on missing key returns -2") {
    auto result = dispatch(store, {bulkStr("TTL"), bulkStr("missing")}, &arena);
    REQUIRE(asInt(result) == -2);
  }

  SECTION("TTL on key without expiry returns -1") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    auto result = dispatch(store, {bulkStr("TTL"), bulkStr("key")}, &arena);
    REQUIRE(asInt(result) == -1);
  }

  SECTION("EXPIRE sets TTL") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")}, &arena);
    auto result = dispatch(
      store, {bulkStr("EXPIRE"), bulkStr("key"), bulkStr("100")}, &arena);
    REQUIRE(asInt(result) == 1);

    auto ttl = dispatch(store, {bulkStr("TTL"), bulkStr("key")}, &arena);
    REQUIRE(asInt(ttl) > 0);
  }

  SECTION("EXPIRE on missing key returns 0") {
    auto result = dispatch(
      store, {bulkStr("EXPIRE"), bulkStr("missing"), bulkStr("10")}, &arena);
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("Unknown command", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;
  std::vector<Type> args;
  auto result = COMMANDS.Dispatch("FOOBAR", args, store, &arena);
  REQUIRE(isError(result));
}

TEST_CASE("Wrong type across operations", "[commands]") {
  std::array<std::byte, 4096> buf{};
  std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
  Storage store;

  dispatch(store, {bulkStr("SET"), bulkStr("str"), bulkStr("hello")}, &arena);
  dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")}, &arena);
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("x")}, &arena);

  SECTION("GET on list") {
    REQUIRE(
      isError(dispatch(store, {bulkStr("GET"), bulkStr("list")}, &arena)));
  }

  SECTION("LPUSH on string") {
    REQUIRE(isError(dispatch(
      store, {bulkStr("LPUSH"), bulkStr("str"), bulkStr("a")}, &arena)));
  }

  SECTION("SADD on list") {
    REQUIRE(isError(dispatch(
      store, {bulkStr("SADD"), bulkStr("list"), bulkStr("a")}, &arena)));
  }

  SECTION("LLEN on set") {
    REQUIRE(
      isError(dispatch(store, {bulkStr("LLEN"), bulkStr("set")}, &arena)));
  }
}
