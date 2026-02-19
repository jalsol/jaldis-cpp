#include "storage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>

TEST_CASE("Storage key operations", "[storage]") {
  Storage store;

  SECTION("Exists returns false for missing key") {
    REQUIRE_FALSE(store.Exists("missing"));
  }

  SECTION("FindOrCreate creates string entry") {
    auto result = store.FindOrCreate<Storage::String>("key");
    REQUIRE(result.has_value());
    **result = "hello";

    auto found = store.Find<Storage::String>("key");
    REQUIRE(found.has_value());
    REQUIRE(**found == "hello");
  }

  SECTION("Exists returns true after creation") {
    store.FindOrCreate<Storage::String>("key");
    REQUIRE(store.Exists("key"));
  }

  SECTION("Erase removes key") {
    store.FindOrCreate<Storage::String>("key");
    REQUIRE(store.Erase("key"));
    REQUIRE_FALSE(store.Exists("key"));
  }

  SECTION("Erase returns false for missing key") {
    REQUIRE_FALSE(store.Erase("missing"));
  }

  SECTION("Keys returns all keys") {
    store.FindOrCreate<Storage::String>("a");
    store.FindOrCreate<Storage::String>("b");
    store.FindOrCreate<Storage::String>("c");

    auto keys = store.Keys();
    REQUIRE(keys.size() == 3);
  }

  SECTION("Clear removes everything") {
    store.FindOrCreate<Storage::String>("a");
    store.FindOrCreate<Storage::String>("b");
    store.Clear();
    REQUIRE(store.Keys().empty());
  }
}

TEST_CASE("Storage type safety", "[storage]") {
  Storage store;

  SECTION("WrongType when accessing string as list") {
    auto result = store.FindOrCreate<Storage::String>("key");
    REQUIRE(result.has_value());

    auto wrong = store.Find<Storage::List>("key");
    REQUIRE_FALSE(wrong.has_value());
    REQUIRE(wrong.error() == Storage::Error::WrongType);
  }

  SECTION("WrongType when accessing list as set") {
    store.FindOrCreate<Storage::List>("key");

    auto wrong = store.Find<Storage::Set>("key");
    REQUIRE_FALSE(wrong.has_value());
    REQUIRE(wrong.error() == Storage::Error::WrongType);
  }

  SECTION("FindOrCreate rejects wrong type") {
    store.FindOrCreate<Storage::String>("key");

    auto wrong = store.FindOrCreate<Storage::List>("key");
    REQUIRE_FALSE(wrong.has_value());
    REQUIRE(wrong.error() == Storage::Error::WrongType);
  }

  SECTION("NotFound for missing key") {
    auto result = store.Find<Storage::String>("missing");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == Storage::Error::NotFound);
  }
}

TEST_CASE("Storage string operations", "[storage]") {
  Storage store;

  SECTION("Set and get string") {
    auto *s = *store.FindOrCreate<Storage::String>("key");
    *s = "hello";

    auto *found = *store.Find<Storage::String>("key");
    REQUIRE(*found == "hello");
  }

  SECTION("Overwrite string value") {
    auto *s = *store.FindOrCreate<Storage::String>("key");
    *s = "first";
    *s = "second";
    REQUIRE(*s == "second");
  }
}

TEST_CASE("Storage list operations", "[storage]") {
  Storage store;

  SECTION("Push and access list") {
    auto *list = *store.FindOrCreate<Storage::List>("mylist");
    list->push_back("a");
    list->push_back("b");
    list->push_front("z");

    REQUIRE(list->size() == 3);
    REQUIRE(list->front() == "z");
    REQUIRE(list->back() == "b");
  }

  SECTION("Pop from list") {
    auto *list = *store.FindOrCreate<Storage::List>("mylist");
    list->push_back("a");
    list->push_back("b");
    list->push_back("c");

    auto front = list->front();
    list->pop_front();
    REQUIRE(front == "a");
    REQUIRE(list->size() == 2);

    auto back = list->back();
    list->pop_back();
    REQUIRE(back == "c");
    REQUIRE(list->size() == 1);
  }

  SECTION("List range access") {
    auto *list = *store.FindOrCreate<Storage::List>("mylist");
    list->push_back("a");
    list->push_back("b");
    list->push_back("c");
    list->push_back("d");

    REQUIRE((*list)[0] == "a");
    REQUIRE((*list)[1] == "b");
    REQUIRE((*list)[2] == "c");
    REQUIRE((*list)[3] == "d");
  }
}

TEST_CASE("Storage set operations", "[storage]") {
  Storage store;

  SECTION("Add and check membership") {
    auto *set = *store.FindOrCreate<Storage::Set>("myset");
    auto [_, inserted] = set->insert("member1");
    REQUIRE(inserted);
    REQUIRE(set->contains("member1"));
  }

  SECTION("Duplicate insert returns false") {
    auto *set = *store.FindOrCreate<Storage::Set>("myset");
    set->insert("member1");
    auto [_, inserted] = set->insert("member1");
    REQUIRE_FALSE(inserted);
  }

  SECTION("Remove from set") {
    auto *set = *store.FindOrCreate<Storage::Set>("myset");
    set->insert("member1");
    REQUIRE(set->erase("member1") == 1);
    REQUIRE_FALSE(set->contains("member1"));
  }

  SECTION("Set size") {
    auto *set = *store.FindOrCreate<Storage::Set>("myset");
    set->insert("a");
    set->insert("b");
    set->insert("c");
    REQUIRE(set->size() == 3);
  }

  SECTION("Set intersection via std algorithms") {
    auto *s1 = *store.FindOrCreate<Storage::Set>("s1");
    s1->insert("a");
    s1->insert("b");
    s1->insert("c");

    auto *s2 = *store.FindOrCreate<Storage::Set>("s2");
    s2->insert("b");
    s2->insert("c");
    s2->insert("d");

    std::vector<std::string> intersection;
    for (const auto &member : *s1) {
      if (s2->contains(member)) {
        intersection.push_back(member);
      }
    }

    REQUIRE(intersection.size() == 2);
    // b and c should be in the intersection
    REQUIRE(std::ranges::find(intersection, std::string{"b"}) != intersection.end());
    REQUIRE(std::ranges::find(intersection, std::string{"c"}) != intersection.end());
  }
}

TEST_CASE("Storage expiration", "[storage]") {
  Storage store;

  SECTION("SetExpiry returns false for missing key") {
    REQUIRE_FALSE(store.SetExpiry("missing", std::chrono::seconds{10}));
  }

  SECTION("SetExpiry returns true for existing key") {
    store.FindOrCreate<Storage::String>("key");
    REQUIRE(store.SetExpiry("key", std::chrono::seconds{10}));
  }

  SECTION("TTL returns -2 for missing key") {
    REQUIRE(store.GetTtl("missing") == -2);
  }

  SECTION("TTL returns -1 for key without expiry") {
    store.FindOrCreate<Storage::String>("key");
    REQUIRE(store.GetTtl("key") == -1);
  }

  SECTION("TTL returns positive for key with expiry") {
    store.FindOrCreate<Storage::String>("key");
    store.SetExpiry("key", std::chrono::seconds{100});
    REQUIRE(store.GetTtl("key") > 0);
  }

  SECTION("Expired key is not found") {
    store.FindOrCreate<Storage::String>("key");
    store.SetExpiry("key", std::chrono::seconds{0});
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    REQUIRE_FALSE(store.Exists("key"));
  }

  SECTION("Sweep removes expired keys") {
    store.FindOrCreate<Storage::String>("a");
    store.FindOrCreate<Storage::String>("b");
    store.SetExpiry("a", std::chrono::seconds{0});
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    store.Sweep();
    REQUIRE_FALSE(store.Exists("a"));
    REQUIRE(store.Exists("b"));
  }
}
