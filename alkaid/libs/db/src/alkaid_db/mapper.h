#ifndef ALKAID_BASE_MAPPER_H
#define ALKAID_BASE_MAPPER_H
#include "alkaid_db/database.h"
#include "alkaid_db/pages.h"
#include "alkaid_storage/storage.h"
#include "alkaid_utils/snowflake.h"

namespace alkaid {
namespace Mapper {
enum class IdType : int32_t {
  INPUT,
  ASSIGN,
};

}

template<typename Entity>
class WriteMapper {
public:
  virtual ~WriteMapper() = default;

protected:
  std::shared_ptr<Database> database;

  bool effective;

private:
  tl::expected<bool, std::string> tableExists(const std::string &tableName) const {
    return database->query<bool>(SQL::SelectSQL(std::vector<std::string>({"count(*)"}), "sqlite_master")
                                 .where_(SQL::Where()
                                         .eq_("type", SQL::Slot::$("type"))
                                         .eq_("name", SQL::Slot::$("name"))
                                        ),
                                 [](sqnice::query &rows) {
                                   return rows.single_value_or<int64_t>(0) > 0;
                                 },
                                 std::unordered_map<std::string, SQL::Val>({{"type", "table"}, {"name", tableName}})
                                );
  }

  virtual void setInsertValuesClause(SQL::InsertSQL &sql, const std::string &idx = "") const {
    std::vector<SQL::Operand> values;
    values.reserve(Entity::TableColumns.size());
    for (auto column: Entity::TableColumns) {
      values.emplace_back(SQL::Slot::$(column + idx));
    }
    sql.values_(values);
  }

  virtual void assignInsertId(Entity &entity) const {
    entity.id = static_cast<int64_t>(Snowflake::genIntId());
  }

  virtual void setInsertValuesClauseParam(SQL::Params &params, const Entity &entity, const std::string &idx = "") const = 0;

  virtual void setUpdateSetClause(SQL::UpdateSQL &sql, const Entity &entity) const = 0;

  virtual void setUpdateSetClauseParam(SQL::Params &params, const Entity &entity) const = 0;

public:
  explicit WriteMapper(const std::string &dbName)
    : database(Storage::getStorage(dbName)->getDatabase(dbName))
    , effective(false) {
    auto exists = tableExists(Entity::TableName);
    if (!exists) return;
    if (*exists) {
      effective = true;
      return;
    }
    auto createRes = database->execute(Entity::createTableSQL());
    if (!createRes) return;
    if (!*createRes) {
      LOGGER_ERROR("{} 表创建失败", Entity::TableName);
      return;
    }
    effective = true;
  }

  explicit WriteMapper(std::shared_ptr<Database> database)
    : database(database)
    , effective(true) {}

  explicit operator bool() const {
    return effective;
  }

  int32_t insert(Entity &entity, const Mapper::IdType idType = Mapper::IdType::ASSIGN) const {
    if (!operator bool()) return 0;
    auto sql = SQL::InsertSQL(Entity::TableName, Entity::TableColumns);
    if (idType == Mapper::IdType::ASSIGN) assignInsertId(entity);
    setInsertValuesClause(sql);
    SQL::Params params;
    params.reserve(Entity::TableColumns.size());
    setInsertValuesClauseParam(params, entity);
    const auto res = database->command(sql, params);
    if (res) return *res;
    LOGGER_ERROR("保存失败：{}", res.error());
    return 0;
  }

  int32_t insert(std::vector<Entity> &entities, const Mapper::IdType idType = Mapper::IdType::ASSIGN, const uint16_t batch = 500) const {
    if (!operator bool() || entities.empty()) return 0;
    const uint16_t batchSize = batch == 0 ? 500 : batch;
    const tl::expected<int32_t, std::string> tx = database->transaction<int32_t>([&entities, idType, batchSize, this](std::shared_ptr<Database> db) -> tl::expected<int32_t, std::string> {
                                                                                   const size_t size = entities.size();
                                                                                   SQL::Params params;
                                                                                   params.reserve(batchSize * Entity::TableColumns.size());
                                                                                   for (size_t i = 0; i < size; i += batchSize) {
                                                                                     params.clear();
                                                                                     auto sql = SQL::InsertSQL(Entity::TableName, Entity::TableColumns);
                                                                                     const size_t end = std::min(i + batchSize, size);
                                                                                     for (size_t j = i; j < end; ++j) {
                                                                                       auto &entity = entities[j];
                                                                                       if (idType == Mapper::IdType::ASSIGN) assignInsertId(entity);
                                                                                       auto idx = std::to_string(j - i);
                                                                                       setInsertValuesClause(sql, idx);
                                                                                       setInsertValuesClauseParam(params, entity, idx);
                                                                                     }
                                                                                     const auto res = db->command(sql, params);
                                                                                     if (res) continue;
                                                                                     return tl::unexpected("保存数据失败：" + res.error());
                                                                                   }
                                                                                   return size;
                                                                                 }
                                                                                );
    if (tx.has_value()) return *tx;
    LOGGER_ERROR("批量保存数据失败：{}", tx.error());
    return 0;
  }

  int32_t update(const Entity &entity, const SQL::Where &where, const SQL::Params &whereClauseParams = {}) const {
    if (!operator bool()) return 0;
    auto sql = SQL::UpdateSQL(Entity::TableName);
    setUpdateSetClause(sql, entity);
    SQL::Params params;
    setUpdateSetClauseParam(params, entity);
    params.insert(whereClauseParams.begin(), whereClauseParams.end());
    const auto res = database->command(sql.where_(where), params);
    if (res) return *res;
    LOGGER_ERROR("更新数据失败：{}", res.error());
    return 0;
  }

  int32_t updateById(const Entity &entity) const {
    if (!operator bool() || !entity.id) return 0;
    return update(entity, SQL::Where()
                  .eq_("id", SQL::Slot::$("id")), {{"id", entity.id}}
                 );
  }

  int32_t updateById(const std::vector<Entity> &entities) const {
    if (!operator bool() || entities.empty()) return 0;
    const auto tx = database->transaction<int32_t>([&entities, this](std::shared_ptr<Database> db) -> tl::expected<int32_t, std::string> {
                                                     for (const auto &entity: entities) {
                                                       auto sql = SQL::UpdateSQL(Entity::TableName);
                                                       setUpdateSetClause(sql, entity);
                                                       SQL::Params params;
                                                       setUpdateSetClauseParam(params, entity);
                                                       params.emplace("id", entity.id);
                                                       const auto res = db->command(sql.where_(SQL::Where().eq_("id", SQL::Slot::$("id"))), params);
                                                       if (res) continue;;
                                                       return tl::unexpected("更新数据失败：" + res.error());
                                                     }
                                                     return entities.size();
                                                   }
                                                  );
    if (tx.has_value()) return *tx;
    LOGGER_ERROR("更新数据失败：{}", tx.error());
    return 0;
  }

  int32_t remove(const SQL::Where &where, const SQL::Params &whereClauseParams = {}) const {
    if (!operator bool()) return 0;
    auto res = database->command(SQL::DeleteSQL(Entity::TableName, where), whereClauseParams);
    if (res) return *res;
    LOGGER_ERROR("删除数据失败：{}", res.error());
    return 0;
  }

  int32_t removeById(const int64_t id) const {
    if (!operator bool()) return 0;
    return remove(SQL::Where().eq_("id", SQL::Slot::$("id")), {{"id", id}});
  }

  int32_t removeById(const std::vector<int64_t> &ids) const {
    if (!operator bool() || ids.empty()) return 0;
    std::vector<SQL::Operand> slots;
    slots.reserve(ids.size());
    SQL::Params params;
    for (size_t i = 0; i < ids.size(); ++i) {
      const std::string slotName = "id" + std::to_string(i);
      slots.emplace_back(SQL::Slot::$(slotName));
      params.emplace(slotName, ids[i]);
    }
    return remove(SQL::Where().in_("id", slots), params);
  }

  template<typename T>
  tl::expected<T, std::string> transaction(const std::function<tl::expected<T, std::string>(std::shared_ptr<Database> db)> &fn) const {
    return database->transaction(fn);
  }
};

template<typename Entity>
class ReadMapper {
public:
  virtual ~ReadMapper() = default;

protected:
  std::shared_ptr<Database> database;

  virtual SQL::SelectSQL buildSelectSql() const {
    return SQL::SelectSQL(Entity::TableColumns, Entity::TableName);
  }

  virtual SQL::Where buildIdWhereClause() const {
    return SQL::Where().eq_("id", SQL::Slot::$("id"));
  }

  virtual SQL::Params buildIdWhereClauseParam(const int64_t id) const {
    return {{"id", id}};
  }

private:
  static void setPageParam(SQL::SelectSQL &sql, const Pages::PageParam &page) {
    const uint32_t size = page.size();
    const uint32_t current = page.current();
    const uint32_t offset = (current - 1) * size;
    sql.limit_(size, offset);
    const auto orders = page.orders();
    if (!orders.empty()) {
      for (const auto &orderItem: orders) {
        sql.orderBy_(orderItem.column, orderItem.asc);
      }
    }
  }

public:
  explicit ReadMapper(const std::string &dbName)
    : database(Storage::getStorage(dbName)->getDatabase(dbName)) {}

  explicit ReadMapper(std::shared_ptr<Database> database)
    : database(database) {}

  std::optional<Entity> getOne(const SQL::Where &where, const SQL::Params &params = {}) const {
    auto entities = database->query<Entity>(buildSelectSql().where_(where).limit_(2), params);
    if (!entities || entities->empty()) return std::nullopt;
    if (entities->size() > 1) {
      LOGGER_ERROR("查询失败：有多条符合条件的结果");
      return std::nullopt;
    }
    return std::optional(std::move((*entities)[0]));
  }

  std::optional<Entity> getById(const int64_t id) const {
    return getOne(buildIdWhereClause(), buildIdWhereClauseParam(id));
  }

  std::vector<Entity> list(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const {
    auto entities = database->query<Entity>(buildSelectSql().where_(where), params);
    if (!entities) return {};
    return *entities;
  }

  Pages::PageResult<Entity> page(const Pages::PageParam &page, const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const {
    const auto total = count(where, params);
    if (total == 0) return Pages::PageResult<Entity>::empty(page);
    auto sql = buildSelectSql().where_(where);
    setPageParam(sql, page);
    auto entities = database->query<Entity>(sql, params);
    if (!entities) return Pages::PageResult<Entity>::empty(page);
    return Pages::PageResult<Entity>::of(page, std::move(*entities), total);
  }

  int64_t count(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const {
    auto sql = buildSelectSql().where_(where).toCountSql();
    auto count = database->query<int64_t>(sql,
                                          [](sqnice::query &rows) {
                                            return rows.single_value_or<int64_t>(0);
                                          }, params
                                         );
    if (!count) return 0;
    return *count;
  }

  bool exists(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const {
    auto sql = buildSelectSql().where_(where).toExistsSql().limit_(1);
    auto exists = database->query<bool>(sql,
                                        [](sqnice::query &rows) {
                                          return rows.single_value_or<int64_t>(0) != 0;
                                        }, params
                                       );
    if (!exists) return false;
    return *exists;
  }
};

}
#endif //ALKAID_BASE_MAPPER_H
