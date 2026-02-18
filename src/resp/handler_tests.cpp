#include "handler.hpp"
#include "values.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory_resource>

using namespace resp;

TEST_CASE("RespHandler switches parsers correctly", "[handler]") {
  std::array<std::byte, 4096> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Parse simple string") {
    RespHandler handler{&arena};
    auto result = handler.Feed("+OK\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<String>(*result.value));
    REQUIRE(std::get<String>(*result.value).value == "OK");
  }

  SECTION("Parse integer") {
    RespHandler handler{&arena};
    auto result = handler.Feed(":42\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Int>(*result.value));
    REQUIRE(std::get<Int>(*result.value).value == 42);
  }

  SECTION("Parse error") {
    RespHandler handler{&arena};
    auto result = handler.Feed("-ERR something\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Error>(*result.value));
    REQUIRE(std::get<Error>(*result.value).value == "ERR something");
  }

  SECTION("Parse bulk string") {
    RespHandler handler{&arena};
    auto result = handler.Feed("$5\r\nhello\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<BulkString>(*result.value));
    REQUIRE(std::get<BulkString>(*result.value).value == "hello");
  }

  SECTION("Parse empty array") {
    RespHandler handler{&arena};
    auto result = handler.Feed("*0\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Array>(*result.value));
    REQUIRE(std::get<Array>(*result.value).value.empty());
  }

  SECTION("Parse array with elements") {
    RespHandler handler{&arena};
    auto result = handler.Feed("*2\r\n+hello\r\n:42\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Array>(*result.value));
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 2);
    REQUIRE(std::holds_alternative<String>(arr[0]));
    REQUIRE(std::holds_alternative<Int>(arr[1]));
  }

  SECTION("Parse nested array") {
    RespHandler handler{&arena};
    auto result = handler.Feed("*1\r\n*1\r\n:99\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 1);
    REQUIRE(std::holds_alternative<Array>(arr[0]));
    auto &nested = std::get<Array>(arr[0]).value;
    REQUIRE(nested.size() == 1);
    REQUIRE(std::get<Int>(nested[0]).value == 99);
  }
}

TEST_CASE("RespHandler handles partial data", "[handler]") {
  std::array<std::byte, 4096> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("String with partial CRLF") {
    RespHandler handler{&arena};

    auto result1 = handler.Feed("+OK");
    REQUIRE(result1.status == ParseStatus::NeedMore);
    REQUIRE_FALSE(result1.value.has_value());
  }

  SECTION("Integer without CRLF") {
    RespHandler handler{&arena};

    auto result = handler.Feed(":42");
    REQUIRE(result.status == ParseStatus::NeedMore);
  }

  SECTION("Bulk string partial length") {
    RespHandler handler{&arena};

    auto result = handler.Feed("$5");
    REQUIRE(result.status == ParseStatus::NeedMore);
  }

  SECTION("Bulk string partial data") {
    RespHandler handler{&arena};

    auto result = handler.Feed("$5\r\nhel");
    REQUIRE(result.status == ParseStatus::NeedMore);
  }
}

TEST_CASE("RespHandler reset functionality", "[handler]") {
  std::array<std::byte, 4096> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Reset after complete parse") {
    RespHandler handler{&arena};

    auto result1 = handler.Feed("+OK\r\n");
    REQUIRE(result1.status == ParseStatus::Done);

    handler.Reset();

    auto result2 = handler.Feed(":42\r\n");
    REQUIRE(result2.status == ParseStatus::Done);
    REQUIRE(std::holds_alternative<Int>(*result2.value));
  }

  SECTION("Reset after partial parse") {
    RespHandler handler{&arena};

    auto result1 = handler.Feed("+OK");
    REQUIRE(result1.status == ParseStatus::NeedMore);

    handler.Reset();

    auto result2 = handler.Feed(":100\r\n");
    REQUIRE(result2.status == ParseStatus::Done);
    REQUIRE(std::holds_alternative<Int>(*result2.value));
  }
}

TEST_CASE("RespHandler with different arena allocators", "[handler][arena]") {
  SECTION("Default allocator") {
    RespHandler handler{};
    auto result = handler.Feed("+OK\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(std::get<String>(*result.value).value == "OK");
  }

  SECTION("Custom monotonic buffer") {
    std::array<std::byte, 1024> buffer;
    std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

    RespHandler handler{&arena};
    auto result = handler.Feed("$5\r\nhello\r\n");

    REQUIRE(result.status == ParseStatus::Done);
    REQUIRE(std::get<BulkString>(*result.value).value == "hello");
  }
}

TEST_CASE("RespHandler handles invalid input", "[handler][error]") {
  std::array<std::byte, 4096> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Invalid type character") {
    RespHandler handler{&arena};
    auto result = handler.Feed("X");

    REQUIRE(result.status == ParseStatus::Cancelled);
  }

  SECTION("Empty input") {
    RespHandler handler{&arena};
    auto result = handler.Feed("");

    REQUIRE(result.status == ParseStatus::NeedMore);
  }
}
