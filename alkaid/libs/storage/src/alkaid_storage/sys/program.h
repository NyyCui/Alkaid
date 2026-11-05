#ifndef ALKAID_PROGRAM_MAPPER_H
#define ALKAID_PROGRAM_MAPPER_H

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "alkaid_db/mapper.h"

namespace alkaid {

struct ProgramEntity {
  std::optional<int64_t> id;
  std::optional<int64_t> orgId;
  std::optional<std::string> identifier;
  std::optional<std::string> icon;
  std::optional<std::string> name;
  std::optional<bool> multiple;
  std::optional<std::string> installTime;
  std::optional<std::string> updateTime;

  static constexpr auto TableName = "program";
  static inline auto TableColumns = std::vector<std::string>({
                                                               "id", "org_id", "identifier",
                                                               "icon", "name", "multiple",
                                                               "install_time", "update_time"
                                                             }
                                                            );

  enum class ColIdx : uint32_t { id = 0, org_id, identifier, icon, name, multiple, install_time, update_time };

  static SQL::CreateTableSQL createTableSQL() {
    return SQL::CreateTableSQL("program")
      .column_("id", SQL::ColumnType::INTEGER)
      .column_("org_id", SQL::ColumnType::INTEGER)
      .column_("identifier", SQL::ColumnType::TEXT)
      .column_("icon", SQL::ColumnType::TEXT)
      .column_("name", SQL::ColumnType::TEXT)
      .column_("multiple", SQL::ColumnType::INTEGER)
      .column_("install_time", SQL::ColumnType::TEXT)
      .column_("update_time", SQL::ColumnType::TEXT);
  }

  ProgramEntity() = default;
};

struct ProgramData {
  int64_t id;
  int64_t orgId;
  std::string programIdentifier;
  std::string icon;
  std::string programName;
  bool multiple;
  std::string installTime;
  std::string updateTime;
  std::string orgName;
  std::string orgIdentifier;
  std::vector<uint8_t> publicKey;
  std::string registerTime;

  static constexpr auto TableName = "program a";
  static inline auto TableColumns = std::vector<std::string>({
                                                               "a.id", "a.org_id", "a.identifier AS program_identifier", "a.icon", "a.name AS program_name", "a.multiple",
                                                               "a.install_time", "a.update_time", "b.name AS org_name", "b.identifier AS org_identifier", "b.public_key", "b.register_time"
                                                             }
                                                            );

  enum class ColIdx : size_t {
    id = 0, org_id, program_identifier, icon, program_name, multiple,
    install_time, update_time, org_name, org_identifier, public_key, register_time
  };

  ProgramData() = default;

  explicit ProgramData(const sqnice::query::row &row);

  friend void to_json(nlohmann::json &j, const ProgramData &p) {
    j = nlohmann::json{
      {"id", p.id},
      {"orgId", p.orgId},
      {"programIdentifier", p.programIdentifier},
      {"icon", p.icon},
      {"programName", p.programName},
      {"multiple", p.multiple},
      {"installTime", p.installTime},
      {"updateTime", p.updateTime},
      {"orgName", p.orgName},
      {"orgIdentifier", p.orgIdentifier},
      {"publicKey", p.publicKey},
      {"registerTime", p.registerTime}
    };
  }
};

class ProgramMapper: public WriteMapper<ProgramEntity>,
                     public ReadMapper<ProgramData> {

  void setInsertValuesClauseParam(SQL::Params &params, const ProgramEntity &entity, const std::string &idx) const override;

  void setUpdateSetClause(SQL::UpdateSQL &sql, const ProgramEntity &entity) const override;

  void setUpdateSetClauseParam(SQL::Params &params, const ProgramEntity &entity) const override;

protected:
  SQL::Where buildIdWhereClause() const override;

  SQL::SelectSQL buildSelectSql() const override;

public:
  ProgramMapper();

  ProgramMapper(std::shared_ptr<Database> database);

  static ProgramMapper &getMapper() {
    static auto mapper = std::make_shared<ProgramMapper>();
    return *mapper;
  }

};

} // namespace alkaid

#endif // ALKAID_PROGRAM_MAPPER_H
