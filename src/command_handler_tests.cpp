#include "commands.hpp"
#include "resp/values.hpp"
#include "storage.hpp"

#include <catch2/catch_test_macros.hpp>

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

Type dispatch(Storage &store, std::initializer_list<Type> args_list) {
  std::vector<Type> all(args_list);
  auto *name_bs = std::get_if<BulkString>(all.data());
  std::string name_upper{name_bs->value};
  std::span<const Type> args{all.data() + 1, all.size() - 1};
  return COMMANDS.Dispatch(std::string_view{name_upper}, args, store);
}

} // namespace

TEST_CASE("PING command", "[commands]") {
  Storage store;

  SECTION("No args returns PONG") {
    auto result = dispatch(store, {bulkStr("PING")});
    REQUIRE(asString(result) == "PONG");
  }

  SECTION("With message echoes it back") {
    auto result = dispatch(store, {bulkStr("PING"), bulkStr("hello")});
    REQUIRE(asBulk(result) == "hello");
  }

  SECTION("Too many args returns error") {
    auto result =
      dispatch(store, {bulkStr("PING"), bulkStr("a"), bulkStr("b")});
    REQUIRE(isError(result));
  }
}

TEST_CASE("SET and GET commands", "[commands]") {
  Storage store;

  SECTION("SET then GET") {
    auto set_result =
      dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("value")});
    REQUIRE(asString(set_result) == "OK");

    auto get_result = dispatch(store, {bulkStr("GET"), bulkStr("key")});
    REQUIRE(asBulk(get_result) == "value");
  }

  SECTION("GET missing key returns nil") {
    auto result = dispatch(store, {bulkStr("GET"), bulkStr("missing")});
    REQUIRE(asBulk(result) == "(nil)");
  }

  SECTION("SET overwrites") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("v1")});
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("v2")});
    auto result = dispatch(store, {bulkStr("GET"), bulkStr("key")});
    REQUIRE(asBulk(result) == "v2");
  }

  SECTION("GET wrong arg count") {
    REQUIRE(isError(dispatch(store, {bulkStr("GET")})));
    REQUIRE(
      isError(dispatch(store, {bulkStr("GET"), bulkStr("a"), bulkStr("b")})));
  }

  SECTION("SET wrong arg count") {
    REQUIRE(isError(dispatch(store, {bulkStr("SET"), bulkStr("key")})));
  }
}

TEST_CASE("DEL command", "[commands]") {
  Storage store;

  SECTION("Delete existing key") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    auto result = dispatch(store, {bulkStr("DEL"), bulkStr("key")});
    REQUIRE(asInt(result) == 1);
    REQUIRE(asBulk(dispatch(store, {bulkStr("GET"), bulkStr("key")})) ==
            "(nil)");
  }

  SECTION("Delete missing key returns 0") {
    auto result = dispatch(store, {bulkStr("DEL"), bulkStr("missing")});
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Delete multiple keys") {
    dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")});
    dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")});
    dispatch(store, {bulkStr("SET"), bulkStr("c"), bulkStr("3")});
    auto result = dispatch(
      store, {bulkStr("DEL"), bulkStr("a"), bulkStr("b"), bulkStr("x")});
    REQUIRE(asInt(result) == 2);
  }
}

TEST_CASE("KEYS command", "[commands]") {
  Storage store;

  SECTION("Empty store") {
    auto result = dispatch(store, {bulkStr("KEYS")});
    REQUIRE(asArray(result).empty());
  }

  SECTION("Returns all keys") {
    dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")});
    dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")});
    auto result = dispatch(store, {bulkStr("KEYS")});
    REQUIRE(asArray(result).size() == 2);
  }
}

TEST_CASE("FLUSHDB command", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("SET"), bulkStr("a"), bulkStr("1")});
  dispatch(store, {bulkStr("SET"), bulkStr("b"), bulkStr("2")});
  auto result = dispatch(store, {bulkStr("FLUSHDB")});
  REQUIRE(asString(result) == "OK");
  REQUIRE(asArray(dispatch(store, {bulkStr("KEYS")})).empty());
}

TEST_CASE("LPUSH and RPUSH commands", "[commands]") {
  Storage store;

  SECTION("LPUSH creates list and returns length") {
    auto r1 =
      dispatch(store, {bulkStr("LPUSH"), bulkStr("list"), bulkStr("a")});
    REQUIRE(asInt(r1) == 1);
    auto r2 =
      dispatch(store, {bulkStr("LPUSH"), bulkStr("list"), bulkStr("b")});
    REQUIRE(asInt(r2) == 2);
  }

  SECTION("RPUSH creates list and returns length") {
    auto r1 =
      dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a")});
    REQUIRE(asInt(r1) == 1);
  }

  SECTION("LPUSH multiple values") {
    auto result = dispatch(store, {bulkStr("LPUSH"), bulkStr("list"),
                                   bulkStr("a"), bulkStr("b"), bulkStr("c")});
    REQUIRE(asInt(result) == 3);
  }

  SECTION("LPUSH on string key returns wrong type") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    auto result =
      dispatch(store, {bulkStr("LPUSH"), bulkStr("key"), bulkStr("a")});
    REQUIRE(isError(result));
  }
}

TEST_CASE("LPOP and RPOP commands", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a")});
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("b")});
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("c")});

  SECTION("LPOP returns first element") {
    auto result = dispatch(store, {bulkStr("LPOP"), bulkStr("list")});
    REQUIRE(asBulk(result) == "a");
  }

  SECTION("RPOP returns last element") {
    auto result = dispatch(store, {bulkStr("RPOP"), bulkStr("list")});
    REQUIRE(asBulk(result) == "c");
  }

  SECTION("LPOP with count returns array") {
    auto result =
      dispatch(store, {bulkStr("LPOP"), bulkStr("list"), bulkStr("2")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "a");
    REQUIRE(asBulk(arr[1]) == "b");
  }

  SECTION("RPOP with count returns array") {
    auto result =
      dispatch(store, {bulkStr("RPOP"), bulkStr("list"), bulkStr("2")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "c");
    REQUIRE(asBulk(arr[1]) == "b");
  }

  SECTION("POP from missing key returns nil") {
    auto result = dispatch(store, {bulkStr("LPOP"), bulkStr("missing")});
    REQUIRE(asBulk(result) == "(nil)");
  }
}

TEST_CASE("LLEN command", "[commands]") {
  Storage store;

  SECTION("Empty/missing list returns 0") {
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("list")});
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Returns correct length") {
    dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a"),
                     bulkStr("b"), bulkStr("c")});
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("list")});
    REQUIRE(asInt(result) == 3);
  }

  SECTION("Wrong type returns error") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    auto result = dispatch(store, {bulkStr("LLEN"), bulkStr("key")});
    REQUIRE(isError(result));
  }
}

TEST_CASE("LRANGE command", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("a"),
                   bulkStr("b"), bulkStr("c"), bulkStr("d")});

  SECTION("Full range") {
    auto result = dispatch(
      store, {bulkStr("LRANGE"), bulkStr("list"), bulkStr("0"), bulkStr("-1")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 4);
    REQUIRE(asBulk(arr[0]) == "a");
    REQUIRE(asBulk(arr[3]) == "d");
  }

  SECTION("Partial range") {
    auto result = dispatch(
      store, {bulkStr("LRANGE"), bulkStr("list"), bulkStr("1"), bulkStr("2")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "b");
    REQUIRE(asBulk(arr[1]) == "c");
  }

  SECTION("Negative indices") {
    auto result = dispatch(store, {bulkStr("LRANGE"), bulkStr("list"),
                                   bulkStr("-2"), bulkStr("-1")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
    REQUIRE(asBulk(arr[0]) == "c");
    REQUIRE(asBulk(arr[1]) == "d");
  }

  SECTION("Missing list returns empty array") {
    auto result = dispatch(store, {bulkStr("LRANGE"), bulkStr("missing"),
                                   bulkStr("0"), bulkStr("-1")});
    REQUIRE(asArray(result).empty());
  }
}

TEST_CASE("SADD command", "[commands]") {
  Storage store;

  SECTION("Add single member") {
    auto result =
      dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")});
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Add duplicate returns 0") {
    dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")});
    auto result =
      dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")});
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Add multiple members") {
    auto result = dispatch(store, {bulkStr("SADD"), bulkStr("set"),
                                   bulkStr("a"), bulkStr("b"), bulkStr("c")});
    REQUIRE(asInt(result) == 3);
  }

  SECTION("Wrong type") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    REQUIRE(isError(
      dispatch(store, {bulkStr("SADD"), bulkStr("key"), bulkStr("a")})));
  }
}

TEST_CASE("SREM command", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b"),
                   bulkStr("c")});

  SECTION("Remove existing member") {
    auto result =
      dispatch(store, {bulkStr("SREM"), bulkStr("set"), bulkStr("a")});
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Remove missing member") {
    auto result =
      dispatch(store, {bulkStr("SREM"), bulkStr("set"), bulkStr("x")});
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Remove from missing key") {
    auto result =
      dispatch(store, {bulkStr("SREM"), bulkStr("missing"), bulkStr("a")});
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("SCARD command", "[commands]") {
  Storage store;

  SECTION("Missing set returns 0") {
    REQUIRE(asInt(dispatch(store, {bulkStr("SCARD"), bulkStr("set")})) == 0);
  }

  SECTION("Returns correct size") {
    dispatch(store,
             {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b")});
    REQUIRE(asInt(dispatch(store, {bulkStr("SCARD"), bulkStr("set")})) == 2);
  }
}

TEST_CASE("SMEMBERS command", "[commands]") {
  Storage store;

  SECTION("Missing set returns empty array") {
    auto result = dispatch(store, {bulkStr("SMEMBERS"), bulkStr("set")});
    REQUIRE(asArray(result).empty());
  }

  SECTION("Returns all members") {
    dispatch(store,
             {bulkStr("SADD"), bulkStr("set"), bulkStr("a"), bulkStr("b")});
    auto result = dispatch(store, {bulkStr("SMEMBERS"), bulkStr("set")});
    REQUIRE(asArray(result).size() == 2);
  }
}

TEST_CASE("SISMEMBER command", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")});

  SECTION("Member exists") {
    auto result =
      dispatch(store, {bulkStr("SISMEMBER"), bulkStr("set"), bulkStr("a")});
    REQUIRE(asInt(result) == 1);
  }

  SECTION("Member does not exist") {
    auto result =
      dispatch(store, {bulkStr("SISMEMBER"), bulkStr("set"), bulkStr("b")});
    REQUIRE(asInt(result) == 0);
  }

  SECTION("Missing key returns 0") {
    auto result =
      dispatch(store, {bulkStr("SISMEMBER"), bulkStr("missing"), bulkStr("a")});
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("SINTER command", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("SADD"), bulkStr("s1"), bulkStr("a"), bulkStr("b"),
                   bulkStr("c")});
  dispatch(store, {bulkStr("SADD"), bulkStr("s2"), bulkStr("b"), bulkStr("c"),
                   bulkStr("d")});

  SECTION("Intersection of two sets") {
    auto result =
      dispatch(store, {bulkStr("SINTER"), bulkStr("s1"), bulkStr("s2")});
    const auto &arr = asArray(result);
    REQUIRE(arr.size() == 2);
  }

  SECTION("Intersection with missing set returns empty") {
    auto result =
      dispatch(store, {bulkStr("SINTER"), bulkStr("s1"), bulkStr("missing")});
    REQUIRE(asArray(result).empty());
  }

  SECTION("Single set returns all members") {
    auto result = dispatch(store, {bulkStr("SINTER"), bulkStr("s1")});
    REQUIRE(asArray(result).size() == 3);
  }
}

TEST_CASE("EXPIRE and TTL commands", "[commands]") {
  Storage store;

  SECTION("TTL on missing key returns -2") {
    auto result = dispatch(store, {bulkStr("TTL"), bulkStr("missing")});
    REQUIRE(asInt(result) == -2);
  }

  SECTION("TTL on key without expiry returns -1") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    auto result = dispatch(store, {bulkStr("TTL"), bulkStr("key")});
    REQUIRE(asInt(result) == -1);
  }

  SECTION("EXPIRE sets TTL") {
    dispatch(store, {bulkStr("SET"), bulkStr("key"), bulkStr("val")});
    auto result =
      dispatch(store, {bulkStr("EXPIRE"), bulkStr("key"), bulkStr("100")});
    REQUIRE(asInt(result) == 1);

    auto ttl = dispatch(store, {bulkStr("TTL"), bulkStr("key")});
    REQUIRE(asInt(ttl) > 0);
  }

  SECTION("EXPIRE on missing key returns 0") {
    auto result =
      dispatch(store, {bulkStr("EXPIRE"), bulkStr("missing"), bulkStr("10")});
    REQUIRE(asInt(result) == 0);
  }
}

TEST_CASE("Unknown command", "[commands]") {
  Storage store;
  std::vector<Type> args;
  auto result = COMMANDS.Dispatch("FOOBAR", args, store);
  REQUIRE(isError(result));
}

TEST_CASE("Wrong type across operations", "[commands]") {
  Storage store;

  dispatch(store, {bulkStr("SET"), bulkStr("str"), bulkStr("hello")});
  dispatch(store, {bulkStr("SADD"), bulkStr("set"), bulkStr("a")});
  dispatch(store, {bulkStr("RPUSH"), bulkStr("list"), bulkStr("x")});

  SECTION("GET on list") {
    REQUIRE(isError(dispatch(store, {bulkStr("GET"), bulkStr("list")})));
  }

  SECTION("LPUSH on string") {
    REQUIRE(isError(
      dispatch(store, {bulkStr("LPUSH"), bulkStr("str"), bulkStr("a")})));
  }

  SECTION("SADD on list") {
    REQUIRE(isError(
      dispatch(store, {bulkStr("SADD"), bulkStr("list"), bulkStr("a")})));
  }

  SECTION("LLEN on set") {
    REQUIRE(isError(dispatch(store, {bulkStr("LLEN"), bulkStr("set")})));
  }
}
