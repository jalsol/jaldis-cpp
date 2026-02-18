#include "serializer.hpp"
#include "values.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory_resource>

using namespace resp;

TEST_CASE("Serialize simple string", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{1024};
  Serializer serializer{&arena};

  SECTION("Basic string") {
    String s{std::pmr::string{"OK", &arena}};
    auto result = serializer.Serialize(Type{s});
    REQUIRE(result == "+OK\r\n");
  }

  SECTION("String with spaces") {
    String s{std::pmr::string{"Hello World", &arena}};
    auto result = serializer.Serialize(Type{s});
    REQUIRE(result == "+Hello World\r\n");
  }

  SECTION("Empty string") {
    String s{std::pmr::string{"", &arena}};
    auto result = serializer.Serialize(Type{s});
    REQUIRE(result == "+\r\n");
  }
}

TEST_CASE("Serialize error", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{1024};
  Serializer serializer{&arena};

  SECTION("Basic error") {
    Error e{std::pmr::string{"ERR", &arena}};
    auto result = serializer.Serialize(Type{e});
    REQUIRE(result == "-ERR\r\n");
  }

  SECTION("Error with message") {
    Error e{std::pmr::string{"ERR unknown command", &arena}};
    auto result = serializer.Serialize(Type{e});
    REQUIRE(result == "-ERR unknown command\r\n");
  }
}

TEST_CASE("Serialize integer", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{1024};
  Serializer serializer{&arena};

  SECTION("Positive integer") {
    Int i{42};
    auto result = serializer.Serialize(Type{i});
    REQUIRE(result == ":42\r\n");
  }

  SECTION("Negative integer") {
    Int i{-100};
    auto result = serializer.Serialize(Type{i});
    REQUIRE(result == ":-100\r\n");
  }

  SECTION("Zero") {
    Int i{0};
    auto result = serializer.Serialize(Type{i});
    REQUIRE(result == ":0\r\n");
  }

  SECTION("Large integer") {
    Int i{123456789};
    auto result = serializer.Serialize(Type{i});
    REQUIRE(result == ":123456789\r\n");
  }
}

TEST_CASE("Serialize bulk string", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{1024};
  Serializer serializer{&arena};

  SECTION("Basic bulk string") {
    BulkString bs{std::pmr::string{"foobar", &arena}};
    auto result = serializer.Serialize(Type{bs});
    REQUIRE(result == "$6\r\nfoobar\r\n");
  }

  SECTION("Empty bulk string") {
    BulkString bs{std::pmr::string{"", &arena}};
    auto result = serializer.Serialize(Type{bs});
    REQUIRE(result == "$0\r\n\r\n");
  }

  SECTION("Bulk string with special characters") {
    BulkString bs{std::pmr::string{"hello\r\nworld", &arena}};
    auto result = serializer.Serialize(Type{bs});
    REQUIRE(result == "$12\r\nhello\r\nworld\r\n");
  }
}

TEST_CASE("Serialize array", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{4096};
  Serializer serializer{&arena};

  SECTION("Empty array") {
    Array arr{std::pmr::vector<Type>{&arena}};
    auto result = serializer.Serialize(Type{arr});
    REQUIRE(result == "*0\r\n");
  }

  SECTION("Array with single integer") {
    std::pmr::vector<Type> elements{&arena};
    elements.push_back(Type{Int{42}});
    Array arr{std::move(elements)};

    auto result = serializer.Serialize(Type{arr});
    REQUIRE(result == "*1\r\n:42\r\n");
  }

  SECTION("Array with multiple integers") {
    std::pmr::vector<Type> elements{&arena};
    elements.emplace_back(Int{1});
    elements.emplace_back(Int{2});
    elements.emplace_back(Int{3});
    Array arr{std::move(elements)};

    auto result = serializer.Serialize(Type{arr});
    REQUIRE(result == "*3\r\n:1\r\n:2\r\n:3\r\n");
  }

  SECTION("Array with mixed types") {
    std::pmr::vector<Type> elements{&arena};
    elements.emplace_back(Int{42});
    elements.emplace_back(String{std::pmr::string{"hello", &arena}});
    elements.emplace_back(BulkString{std::pmr::string{"world", &arena}});
    Array arr{std::move(elements)};

    auto result = serializer.Serialize(Type{arr});
    REQUIRE(result == "*3\r\n:42\r\n+hello\r\n$5\r\nworld\r\n");
  }

  SECTION("Nested arrays") {
    std::pmr::vector<Type> inner{&arena};
    inner.emplace_back(Int{1});
    inner.emplace_back(Int{2});

    std::pmr::vector<Type> outer{&arena};
    outer.emplace_back(Array{std::move(inner)});
    outer.emplace_back(Int{3});

    Array arr{std::move(outer)};
    auto result = serializer.Serialize(Type{arr});
    REQUIRE(result == "*2\r\n*2\r\n:1\r\n:2\r\n:3\r\n");
  }
}

TEST_CASE("Reusable Serializer", "[serializer]") {
  std::pmr::monotonic_buffer_resource arena{4096};
  Serializer serializer{&arena};

  SECTION("Serialize multiple values") {
    auto result1 = serializer.Serialize(Type{Int{42}});
    REQUIRE(result1 == ":42\r\n");

    auto result2 =
      serializer.Serialize(Type{String{std::pmr::string{"OK", &arena}}});
    REQUIRE(result2 == "+OK\r\n");

    auto result3 = serializer.Serialize(Type{Int{100}});
    REQUIRE(result3 == ":100\r\n");
  }

  SECTION("Buffer reuse maintains capacity") {
    // First serialize large value
    BulkString large{std::pmr::string(1000, 'x', &arena)};
    serializer.Serialize(Type{large});

    // Then small value should not need reallocation
    auto result = serializer.Serialize(Type{Int{1}});
    REQUIRE(result == ":1\r\n");
  }
}

TEST_CASE("Size calculation", "[serializer][size]") {
  std::pmr::monotonic_buffer_resource arena{1024};

  SECTION("String size") {
    String s{std::pmr::string{"OK", &arena}};
    REQUIRE(TypeSerializer<String>::CalculateSize(s) == 5); // +OK\r\n
  }

  SECTION("Integer size") {
    REQUIRE(TypeSerializer<Int>::CalculateSize(Int{0}) == 4);    // :0\r\n
    REQUIRE(TypeSerializer<Int>::CalculateSize(Int{42}) == 5);   // :42\r\n
    REQUIRE(TypeSerializer<Int>::CalculateSize(Int{-100}) == 7); // :-100\r\n
    REQUIRE(TypeSerializer<Int>::CalculateSize(Int{123456}) ==
            9); // :123456\r\n
  }

  SECTION("Bulk string size") {
    BulkString bs{std::pmr::string{"foobar", &arena}};
    REQUIRE(TypeSerializer<BulkString>::CalculateSize(bs) ==
            12); // $6\r\nfoobar\r\n
  }

  SECTION("Array size") {
    std::pmr::vector<Type> elements{&arena};
    elements.emplace_back(Int{1});
    elements.emplace_back(Int{2});
    Array arr{std::move(elements)};
    REQUIRE(TypeSerializer<Array>::CalculateSize(arr) ==
            12); // *2\r\n:1\r\n:2\r\n
  }
}
