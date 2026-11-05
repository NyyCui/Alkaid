#ifndef ALKAID_ORGANIZATION_MAPPER_H
#define ALKAID_ORGANIZATION_MAPPER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "alkaid_db/mapper.h"

namespace alkaid {

struct OrganizationEntity {
  std::optional<int64_t> id;
  std::optional<std::string> name;
  std::optional<std::string> identifier;
  std::optional<std::vector<uint8_t>> publicKey;
  std::optional<std::vector<uint8_t>> privateKey;
  std::optional<std::string> registerTime;

  static constexpr auto TableName = "organization";
  static inline auto TableColumns = std::vector<std::string>({"id", "name", "identifier", "public_key", "private_key", "register_time"}
                                                            );

  enum class ColIdx : uint32_t { id = 0, name, identifier, public_key, private_key, register_time };

  static SQL::CreateTableSQL createTableSQL() {
    return SQL::CreateTableSQL("organization")
      .column_("id", SQL::ColumnType::INTEGER)
      .column_("name", SQL::ColumnType::TEXT)
      .column_("identifier", SQL::ColumnType::TEXT)
      .column_("public_key", SQL::ColumnType::BLOB)
      .column_("private_key", SQL::ColumnType::BLOB)
      .column_("register_time", SQL::ColumnType::TEXT);
  }

  OrganizationEntity() = default;
};

struct OrganizationData {
  int64_t id;
  std::string name;
  std::string identifier;
  std::vector<uint8_t> publicKey;
  std::vector<uint8_t> privateKey;
  std::string registerTime;

  static constexpr auto TableName = "organization";
  static inline auto TableColumns = std::vector<std::string>({"id", "name", "identifier", "public_key", "private_key", "register_time"}
                                                            );

  enum class ColIdx : uint32_t { id = 0, name, identifier, public_key, private_key, register_time };

  OrganizationData() = default;

  explicit OrganizationData(const sqnice::query::row &row);
};

class OrganizationMapper: public WriteMapper<OrganizationEntity>,
                          public ReadMapper<OrganizationData> {

  void setInsertValuesClauseParam(SQL::Params &params, const OrganizationEntity &entity, const std::string &idx) const override;

  void setUpdateSetClause(SQL::UpdateSQL &sql, const OrganizationEntity &entity) const override;

  void setUpdateSetClauseParam(SQL::Params &params, const OrganizationEntity &entity) const override;

public:
  OrganizationMapper();

  OrganizationMapper(std::shared_ptr<Database> database);

  static OrganizationMapper &getMapper() {
    static auto mapper = std::make_shared<OrganizationMapper>();
    return *mapper;
  }

};

/*
class OrganizationMapper {
  std::shared_ptr<Database> database;

  static tl::expected<size_t, std::string> update(const Database &db, const OrganizationOptionEntity &entity, const SQL::Where &where, const SQL::Params &params = {});

public:
  OrganizationMapper();

  static OrganizationMapper &getMapper();

  bool createTable() const;

  const Database &getDatabase() const;

  bool save(OrganizationEntity &entity, bool genId = true) const;

  bool saveBatch(std::vector<OrganizationEntity> &entities, size_t batch = 500) const;

  size_t update(const OrganizationOptionEntity &entity, const SQL::Where &where, const SQL::Params &params = {}) const;

  size_t updateBatch(const std::vector<OrganizationOptionEntity> &entities, const SQL::Where &where, const SQL::Params &params = {}) const;

  size_t updateBatchById(const std::vector<OrganizationOptionEntity> &entities) const;

  size_t updateById(const OrganizationOptionEntity &entity) const;

  size_t remove(const SQL::Where &where, const SQL::Params &params = {}) const;

  size_t removeById(int64_t id) const;

  size_t removeByIds(const std::vector<int64_t> &ids) const;

  std::optional<OrganizationEntity> getOne(const SQL::Where &where, const SQL::Params &params = {}) const;

  std::optional<OrganizationEntity> getById(int64_t id) const;

  std::vector<OrganizationEntity> list(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const;

  Pages::PageResult<OrganizationEntity> page(const Pages::PageParam &page, const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const;

  int64_t count(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const;

  bool exists(const SQL::Where &where = SQL::Where(), const SQL::Params &params = {}) const;
};*/

} // namespace alkaid

#endif // ALKAID_ORGANIZATION_MAPPER_H
