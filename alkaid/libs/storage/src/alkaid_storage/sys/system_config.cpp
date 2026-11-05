#include "system_config.h"

#include "alkaid_utils/snowflake.h"

namespace alkaid {

/*
SystemConfigMapper::SystemConfigMapper() : database(Storage::getStorage("sys")
                                                   ->getDatabase("sys")) {}
bool SystemConfigMapper::save(SystemConfigEntity &entity) const {
  return database->save(SQL::createInsertSQL("system_config", {"id", "install_path"})
                            .values_({SQL::SQLParam(entity.id), SQL::SQLParam(entity.installPath)}))
      .has_value();
}
bool SystemConfigMapper::saveBatch(std::vector<SystemConfigEntity> &entities, const size_t batch) const {
  if (entities.empty()) return false;
  const size_t size = entities.size();
  bool success = true;
  database->transaction([&](const sqnice::borrowed_writeable_database &db) {
    for (size_t i = 0; i < size; i += batch) {
      auto sql = SQL::createInsertSQL("system_config", {"id", "install_path"});
      const size_t end = std::min(i + batch, size);
      for (size_t j = i; j < end; ++j) {
        auto &entity = entities[j];
        entity.id = Snowflake::genIntId();
        sql.values_({SQL::SQLParam(entity.id), SQL::SQLParam(entity.installPath)});
      }
      if (!database->save(db, sql).has_value()) {
        success = false;
        return;
      }
    }
  });
  return success;
}
bool SystemConfigMapper::removeById(const int64_t id) const {
  return database->del(SQL::createDeleteSQL("system_config", SQL::createWhere().eq_("id", id))).has_value();
}

bool SystemConfigMapper::removeByIds(const std::vector<int64_t> &ids) const {
  std::vector<SQL::SQLParam> params;
  params.reserve(ids.size());
  for (const int64_t id : ids) {
    params.emplace_back(id);
  }
  return database->del(SQL::createDeleteSQL("system_config", SQL::createWhere().in_("id", params))).has_value();
}
size_t SystemConfigMapper::updateById(const SystemConfigEntity &entity) const {
  return database->upd(SQL::createUpdateSQL("system_config")
                           .set_("install_path", entity.installPath)
                           .where_(SQL::createWhere().eq_("id", entity.id)))
      .value_or(0);
}
std::optional<SystemConfigEntity> SystemConfigMapper::getById(const int64_t id) const {
  return database->queryOne<SystemConfigEntity>(SQL::createSelectSQL("system_config", {"id", "install_path"})
                                                    .where_(SQL::createWhere().eq_("id", id)));
}
std::vector<SystemConfigEntity> SystemConfigMapper::list() const {
  return database->queryList<SystemConfigEntity>(SQL::createSelectSQL("system_config", {"id", "install_path"}));
}
std::vector<SystemConfigEntity> SystemConfigMapper::paging(const uint32_t page, const uint32_t size) const {
  return database->queryList<SystemConfigEntity>(SQL::createSelectSQL("system_config", {"id", "install_path"})
                                                     .limit_((page - 1) * size, size));
}
bool SystemConfigMapper::createTable() const {
  if (database->tableExists("system_config")) return true;
  return database->create(SQL::createCreateTableSQL("system_config")
                              .column_("id", SQL::Type::INTEGER, true)
                              .column_("install_path", SQL::Type::TEXT, true)
                              .primaryKey_("id"));
}
SystemConfigMapper &SystemConfigMapper::getMapper() {
  static auto mapper = std::make_shared<SystemConfigMapper>();
  return *mapper;
}*/

} // namespace alkaid
