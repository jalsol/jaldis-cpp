#include "storage.hpp"

#include <algorithm>

Storage::Entry *Storage::FindEntry(std::string_view key) {
  auto it = data_.find(key);
  if (it == data_.end()) {
    return nullptr;
  }

  if (it->second.Expired(Clock::now())) {
    data_.erase(it);
    return nullptr;
  }

  return &it->second;
}

bool Storage::Exists(std::string_view key) { return FindEntry(key) != nullptr; }

bool Storage::Erase(std::string_view key) {
  auto it = data_.find(key);
  if (it == data_.end()) {
    return false;
  }
  data_.erase(it);
  return true;
}

std::vector<std::string_view> Storage::Keys() {
  std::vector<std::string_view> result;
  result.reserve(data_.size());
  auto now = Clock::now();

  auto it = data_.begin();
  while (it != data_.end()) {
    if (it->second.Expired(now)) {
      it = data_.erase(it);
    } else {
      result.emplace_back(it->first);
      ++it;
    }
  }

  return result;
}

void Storage::Clear() { data_.clear(); }

template <typename T> Storage::Result<T *> Storage::Find(std::string_view key) {
  auto *entry = FindEntry(key);
  if (!entry) {
    return std::unexpected{Error::NotFound};
  }

  auto *val = std::get_if<T>(&entry->value);
  if (!val) {
    return std::unexpected{Error::WrongType};
  }

  return val;
}

template <typename T>
Storage::Result<T *> Storage::FindOrCreate(std::string_view key) {
  auto *entry = FindEntry(key);

  if (!entry) {
    auto [it, _] = data_.emplace(std::string{key}, Entry{T{}, std::nullopt});
    return &std::get<T>(it->second.value);
  }

  auto *val = std::get_if<T>(&entry->value);
  if (!val) {
    return std::unexpected{Error::WrongType};
  }

  return val;
}

bool Storage::SetExpiry(std::string_view key, std::chrono::seconds ttl) {
  auto *entry = FindEntry(key);
  if (!entry) {
    return false;
  }
  entry->expires_at = Clock::now() + ttl;
  return true;
}

int Storage::GetTtl(std::string_view key) {
  auto *entry = FindEntry(key);
  if (!entry) {
    return -2;
  }
  if (!entry->expires_at) {
    return -1;
  }

  auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
    *entry->expires_at - Clock::now());

  return std::max(0, static_cast<int>(remaining.count()));
}

void Storage::Sweep(std::size_t max_checks) {
  const auto bucket_count = data_.bucket_count();
  if (bucket_count == 0) {
    return;
  }

  auto now = Clock::now();
  auto checked = 0;

  for (auto attempt = 0; checked < max_checks && attempt < max_checks * 2;
       ++attempt) {
    const auto bucket = rng_() % bucket_count;
    for (auto it = data_.begin(bucket); it != data_.end(bucket); ++it) {
      if (checked >= max_checks) {
        break;
      }
      if (it->second.Expired(now)) {
        data_.erase(it->first);
        break; // bucket iterators invalidated after erase
      }
      ++checked;
    }
  }
}

// Explicit instantiations
template Storage::Result<Storage::String *>
  Storage::Find<Storage::String>(std::string_view);
template Storage::Result<Storage::List *>
  Storage::Find<Storage::List>(std::string_view);
template Storage::Result<Storage::Set *>
  Storage::Find<Storage::Set>(std::string_view);

template Storage::Result<Storage::String *>
  Storage::FindOrCreate<Storage::String>(std::string_view);
template Storage::Result<Storage::List *>
  Storage::FindOrCreate<Storage::List>(std::string_view);
template Storage::Result<Storage::Set *>
  Storage::FindOrCreate<Storage::Set>(std::string_view);
