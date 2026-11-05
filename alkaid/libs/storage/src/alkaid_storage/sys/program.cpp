#include "program.h"
#include "alkaid_db/type_handler.h"

namespace alkaid {
ProgramData::ProgramData(const sqnice::query::row &row)
  : id(row[static_cast<unsigned>(ColIdx::id)].get<int64_t>())
  , orgId(row[static_cast<unsigned>(ColIdx::org_id)].get<int64_t>())
  , programIdentifier(row[static_cast<unsigned>(ColIdx::program_identifier)].get<std::string>())
  , icon(row[static_cast<unsigned>(ColIdx::icon)].get<std::string>())
  , programName(row[static_cast<unsigned>(ColIdx::program_name)].get<std::string>())
  , multiple(row[static_cast<unsigned>(ColIdx::multiple)].get<int64_t>() != 0)
  , installTime(row[static_cast<unsigned>(ColIdx::install_time)].get<std::string>())
  , updateTime(row[static_cast<unsigned>(ColIdx::update_time)].get<std::string>())
  , orgName(row[static_cast<unsigned>(ColIdx::org_name)].get<std::string>())
  , orgIdentifier(row[static_cast<unsigned>(ColIdx::org_identifier)].get<std::string>())
  , publicKey(SQL::TypeHandler::handler<std::vector<uint8_t>>(row[static_cast<unsigned>(ColIdx::public_key)]))
  , registerTime(row[static_cast<unsigned>(ColIdx::register_time)].get<std::string>()) {}

void ProgramMapper::setInsertValuesClauseParam(SQL::Params &params, const ProgramEntity &entity, const std::string &idx) const {
  params.emplace("id" + idx, entity.id);
  params.emplace("org_id" + idx, entity.orgId);
  params.emplace("identifier" + idx, entity.identifier);
  params.emplace("icon" + idx, entity.icon);
  params.emplace("name" + idx, entity.name);
  params.emplace("multiple" + idx, entity.multiple);
  params.emplace("install_time" + idx, entity.installTime);
  params.emplace("update_time" + idx, entity.updateTime);
}

void ProgramMapper::setUpdateSetClause(SQL::UpdateSQL &sql, const ProgramEntity &entity) const {
  sql.if_(entity.orgId.has_value(), [](auto &it) {
            it.set_("org_id", SQL::Slot::$("org_id"));
          }
         );
  sql.if_(entity.identifier.has_value(), [](auto &it) {
            it.set_("identifier", SQL::Slot::$("identifier"));
          }
         );
  sql.if_(entity.icon.has_value(), [](auto &it) {
            it.set_("icon", SQL::Slot::$("icon"));
          }
         );
  sql.if_(entity.name.has_value(), [](auto &it) {
            it.set_("name", SQL::Slot::$("name"));
          }
         );
  sql.if_(entity.multiple.has_value(), [](auto &it) {
            it.set_("multiple", SQL::Slot::$("multiple"));
          }
         );
  sql.if_(entity.installTime.has_value(), [](auto &it) {
            it.set_("install_time", SQL::Slot::$("install_time"));
          }
         );
  sql.if_(entity.updateTime.has_value(), [](auto &it) {
            it.set_("update_time", SQL::Slot::$("update_time"));
          }
         );
}

void ProgramMapper::setUpdateSetClauseParam(SQL::Params &params, const ProgramEntity &entity) const {
  if (entity.orgId) params.emplace("org_id", entity.orgId);
  if (entity.identifier) params.emplace("identifier", entity.identifier);
  if (entity.icon) params.emplace("icon", entity.icon);
  if (entity.name) params.emplace("name", entity.name);
  if (entity.multiple) params.emplace("multiple", entity.multiple);
  if (entity.installTime) params.emplace("install_time", entity.installTime);
  if (entity.updateTime) params.emplace("update_time", entity.updateTime);
}

SQL::Where ProgramMapper::buildIdWhereClause() const {
  return SQL::Where().eq_("a.id", SQL::Slot::$("id"));
}

SQL::SelectSQL ProgramMapper::buildSelectSql() const {
  return ReadMapper::buildSelectSql()
    .innerJoin_("organization", "b", SQL::On().eq_("b.id", SQL::Val::colVal("a.org_id")));
}

ProgramMapper::ProgramMapper() : WriteMapper("sys")
                               , ReadMapper("sys") {}

ProgramMapper::ProgramMapper(std::shared_ptr<Database> database) : WriteMapper(database)
                                                                 , ReadMapper(database) {}
}
