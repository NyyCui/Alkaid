#ifndef ALKAID_STORAGE_H
#define ALKAID_STORAGE_H
#include "alkaid_db/database.h"

#include <shared_mutex>

namespace alkaid {

class Storage {
  std::string path_;
  std::shared_mutex databasesMutex_;
  std::unordered_map<std::string, std::shared_ptr<Database>> databases_;

  inline static std::shared_mutex storagesMutex_;
  inline static std::unordered_map<std::string, std::shared_ptr<Storage>> storages_;

public:
  explicit Storage(const std::string &path);
  static std::shared_ptr<Storage> getStorage(const std::string &name, const std::string &path = "");
  std::shared_ptr<Database> getDatabase(const std::string &name);
};

} // namespace alkaid

#endif // ALKAID_STORAGE_H
