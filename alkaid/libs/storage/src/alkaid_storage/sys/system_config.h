#ifndef ALKAID_SYSTEM_CONFIG_MAPPER_H
#define ALKAID_SYSTEM_CONFIG_MAPPER_H

#include "alkaid_storage/storage.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace alkaid {
/*
struct SystemConfigEntity {
  int64_t id;
  std::string installPath;
  SystemConfigEntity() = default;
  explicit SystemConfigEntity(const sqnice::query::row &row)
      : id(row[0].get<int64_t>()),
        installPath(row[1].get<std::string>()) {
  }
};

class SystemConfigMapper {
  std::shared_ptr<Database> database;

public:
  SystemConfigMapper();
  bool save(SystemConfigEntity &entity) const;
  bool saveBatch(std::vector<SystemConfigEntity> &entities, size_t batch = 500) const;
  bool removeById(int64_t id) const;
  bool removeByIds(const std::vector<int64_t> &ids) const;
  size_t updateById(const SystemConfigEntity &entity) const;
  std::optional<SystemConfigEntity> getById(int64_t id) const;
  std::vector<SystemConfigEntity> list() const;
  std::vector<SystemConfigEntity> paging(uint32_t page, uint32_t size) const;
  bool createTable() const;
  static SystemConfigMapper &getMapper();
};
*/

} // namespace alkaid

#endif // ALKAID_SYSTEM_CONFIG_MAPPER_H
