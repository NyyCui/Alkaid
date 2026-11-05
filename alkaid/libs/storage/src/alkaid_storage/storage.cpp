#include "storage.h"

#include "alkaid_utils/process_util.h"

#include <shared_mutex>

namespace alkaid {

std::shared_ptr<Storage> Storage::getStorage(const std::string &name, const std::string &path) {
  namespace fs = std::filesystem;
  const auto paths = (path.empty() ? ProcessUtil::homeDir() : std::filesystem::path(path)) / STORAGE_PATH;
  if (!fs::exists(paths)) fs::create_directories(paths);

  {
    std::shared_lock lock(storagesMutex_);
    const auto it = storages_.find(name);
    if (it != storages_.end()) {
      return it->second;
    }
  }
  {
    std::unique_lock lock(storagesMutex_);
    const auto it = storages_.find(name);
    if (it == storages_.end()) {
      auto [newIt, inserted] = storages_.emplace(name, std::make_shared<Storage>(paths.string()));
      return newIt->second;
    }
    return it->second;
  }
}

Storage::Storage(const std::string &path) : path_(path) {}

std::shared_ptr<Database> Storage::getDatabase(const std::string &name) {
  {
    std::shared_lock lock(databasesMutex_);
    const auto it = databases_.find(name);
    if (it != databases_.end()) {
      return it->second;
    }
  }
  {
    std::unique_lock lock(databasesMutex_);
    const auto it = databases_.find(name);
    if (it == databases_.end()) {
      auto [newIt, inserted] = databases_.emplace(name, std::make_shared<Database>(path_ + "/" + name + ".db"));
      return newIt->second;
    }
    return it->second;
  }
}

} // namespace alkaid
