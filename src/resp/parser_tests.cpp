#include "parser.hpp"
#include "values.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory_resource>

using namespace resp;

TEST_CASE("IntParser parses integers", "[parser][int]") {
  std::array<std::byte, 1024> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Positive integer") {
    IntParser parser{&arena};
    auto result = parser.Feed("42\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(result.consumed == 4);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Int>(*result.value));
    REQUIRE(std::get<Int>(*result.value).value == 42);
  }

  SECTION("Negative integer") {
    IntParser parser{&arena};
    auto result = parser.Feed("-100\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(result.consumed == 6);
    REQUIRE(std::get<Int>(*result.value).value == -100);
  }

  SECTION("Zero") {
    IntParser parser{&arena};
    auto result = parser.Feed("0\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<Int>(*result.value).value == 0);
  }

  SECTION("Partial data needs more") {
    IntParser parser{&arena};
    auto result = parser.Feed("42");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }
}

TEST_CASE("StringParser parses simple strings", "[parser][string]") {
  std::array<std::byte, 1024> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Simple string") {
    StringParser parser{&arena};
    auto result = parser.Feed("OK\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(result.consumed == 4);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<String>(*result.value));
    REQUIRE(std::get<String>(*result.value).value == "OK");
  }

  SECTION("String with spaces") {
    StringParser parser{&arena};
    auto result = parser.Feed("Hello World\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<String>(*result.value).value == "Hello World");
  }

  SECTION("Empty string") {
    StringParser parser{&arena};
    auto result = parser.Feed("\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<String>(*result.value).value == "");
  }

  SECTION("Partial data") {
    StringParser parser{&arena};
    auto result = parser.Feed("OK");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }
}

TEST_CASE("ErrorParser parses error strings", "[parser][error]") {
  std::array<std::byte, 1024> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Simple error") {
    ErrorParser parser{&arena};
    auto result = parser.Feed("ERR\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Error>(*result.value));
    REQUIRE(std::get<Error>(*result.value).value == "ERR");
  }

  SECTION("Error with message") {
    ErrorParser parser{&arena};
    auto result = parser.Feed("ERR unknown command\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<Error>(*result.value).value == "ERR unknown command");
  }
}

TEST_CASE("BulkStringParser parses bulk strings", "[parser][bulkstring]") {
  std::array<std::byte, 1024> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Simple bulk string") {
    BulkStringParser parser{&arena};
    auto result = parser.Feed("5\r\nhello\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(result.consumed == 10);
    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<BulkString>(*result.value));
    REQUIRE(std::get<BulkString>(*result.value).value == "hello");
  }

  SECTION("Empty bulk string") {
    BulkStringParser parser{&arena};
    auto result = parser.Feed("0\r\n\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<BulkString>(*result.value).value == "");
  }

  SECTION("Bulk string with special characters") {
    BulkStringParser parser{&arena};
    auto result = parser.Feed("12\r\nhello\r\nworld\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::get<BulkString>(*result.value).value == "hello\r\nworld");
  }

  SECTION("Partial length") {
    BulkStringParser parser{&arena};
    auto result = parser.Feed("5");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }

  SECTION("Partial data") {
    BulkStringParser parser{&arena};
    auto result = parser.Feed("5\r\nhel");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }
}

TEST_CASE("ArrayParser parses arrays", "[parser][array]") {
  std::array<std::byte, 1024> buffer;
  std::pmr::monotonic_buffer_resource arena{buffer.data(), buffer.size()};

  SECTION("Empty array") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("0\r\n");

    REQUIRE(result.value.has_value());
    REQUIRE(std::holds_alternative<Array>(*result.value));
    REQUIRE(std::get<Array>(*result.value).value.empty());
  }

  SECTION("Array with single integer") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("1\r\n:42\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 1);
    REQUIRE(std::holds_alternative<Int>(arr[0]));
    REQUIRE(std::get<Int>(arr[0]).value == 42);
  }

  SECTION("Array with single string") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("1\r\n+OK\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 1);
    REQUIRE(std::holds_alternative<String>(arr[0]));
    REQUIRE(std::get<String>(arr[0]).value == "OK");
  }

  SECTION("Array with multiple elements") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("3\r\n:1\r\n:2\r\n:3\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 3);
    REQUIRE(std::get<Int>(arr[0]).value == 1);
    REQUIRE(std::get<Int>(arr[1]).value == 2);
    REQUIRE(std::get<Int>(arr[2]).value == 3);
  }

  SECTION("Array with mixed types") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("3\r\n+hello\r\n:123\r\n-ERR\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 3);
    REQUIRE(std::holds_alternative<String>(arr[0]));
    REQUIRE(std::holds_alternative<Int>(arr[1]));
    REQUIRE(std::holds_alternative<Error>(arr[2]));
    REQUIRE(std::get<String>(arr[0]).value == "hello");
    REQUIRE(std::get<Int>(arr[1]).value == 123);
    REQUIRE(std::get<Error>(arr[2]).value == "ERR");
  }

  SECTION("Array with bulk strings") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("2\r\n$5\r\nhello\r\n$5\r\nworld\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 2);
    REQUIRE(std::get<BulkString>(arr[0]).value == "hello");
    REQUIRE(std::get<BulkString>(arr[1]).value == "world");
  }

  SECTION("Nested array") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("2\r\n:1\r\n*2\r\n:2\r\n:3\r\n");

    REQUIRE(result.value.has_value());
    auto &arr = std::get<Array>(*result.value).value;
    REQUIRE(arr.size() == 2);
    REQUIRE(std::holds_alternative<Int>(arr[0]));
    REQUIRE(std::get<Int>(arr[0]).value == 1);

    REQUIRE(std::holds_alternative<Array>(arr[1]));
    auto &nested = std::get<Array>(arr[1]).value;
    REQUIRE(nested.size() == 2);
    REQUIRE(std::get<Int>(nested[0]).value == 2);
    REQUIRE(std::get<Int>(nested[1]).value == 3);
  }

  SECTION("Partial array length") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("3");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }

  SECTION("Partial array data") {
    ArrayParser parser{&arena};
    auto result = parser.Feed("2\r\n:1\r\n");

    REQUIRE_FALSE(result.value.has_value());
    REQUIRE(result.value.error() == ParseStatus::NeedMore);
  }

  SECTION("Multi-call array parsing with type bytes") {
    ArrayParser parser{&arena};

    auto result1 = parser.Feed("2\r\n");
    REQUIRE_FALSE(result1.value.has_value());
    REQUIRE(result1.value.error() == ParseStatus::NeedMore);

    auto result2 = parser.Feed("+hello\r\n");
    REQUIRE_FALSE(result2.value.has_value());
    REQUIRE(result2.value.error() == ParseStatus::NeedMore);

    auto result3 = parser.Feed(":42\r\n");
    REQUIRE(result3.value.has_value());

    auto &arr = std::get<Array>(*result3.value).value;
    REQUIRE(arr.size() == 2);
    REQUIRE(std::get<String>(arr[0]).value == "hello");
    REQUIRE(std::get<Int>(arr[1]).value == 42);
  }
}
