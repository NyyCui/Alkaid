#include "organization.h"
#include "alkaid_db/type_handler.h"

namespace alkaid {

OrganizationData::OrganizationData(const sqnice::query::row &row)
  : id(row[static_cast<unsigned>(ColIdx::id)].get<int64_t>())
  , name(row[static_cast<unsigned>(ColIdx::name)].get<std::string>())
  , identifier(row[static_cast<unsigned>(ColIdx::identifier)].get<std::string>())
  , publicKey(SQL::TypeHandler::handler<std::vector<uint8_t>>(row[static_cast<unsigned>(ColIdx::public_key)]))
  , privateKey(SQL::TypeHandler::handler<std::vector<uint8_t>>(row[static_cast<unsigned>(ColIdx::private_key)]))
  , registerTime(row[static_cast<unsigned>(ColIdx::register_time)].get<std::string>()) {}

void OrganizationMapper::setInsertValuesClauseParam(SQL::Params &params, const OrganizationEntity &entity, const std::string &idx) const {
  params.emplace("id" + idx, entity.id);
  params.emplace("name" + idx, entity.name);
  params.emplace("identifier" + idx, entity.identifier);
  params.emplace("public_key" + idx, entity.publicKey);
  params.emplace("private_key" + idx, entity.privateKey);
  params.emplace("register_time" + idx, entity.registerTime);
}

void OrganizationMapper::setUpdateSetClause(SQL::UpdateSQL &sql, const OrganizationEntity &entity) const {
  sql.if_(entity.name.has_value(), [](auto &it) {
            it.set_("name", SQL::Slot::$("name"));
          }
         );
  sql.if_(entity.identifier.has_value(), [](auto &it) {
            it.set_("identifier", SQL::Slot::$("identifier"));
          }
         );
  sql.if_(entity.publicKey.has_value(), [](auto &it) {
            it.set_("public_key", SQL::Slot::$("public_key"));
          }
         );
  sql.if_(entity.privateKey.has_value(), [](auto &it) {
            it.set_("private_key", SQL::Slot::$("private_key"));
          }
         );
  sql.if_(entity.registerTime.has_value(), [](auto &it) {
            it.set_("register_time", SQL::Slot::$("register_time"));
          }
         );
}

void OrganizationMapper::setUpdateSetClauseParam(SQL::Params &params, const OrganizationEntity &entity) const {
  if (entity.name) params.emplace("name", entity.name);
  if (entity.identifier) params.emplace("identifier", entity.identifier);
  if (entity.publicKey) params.emplace("public_key", entity.publicKey);
  if (entity.privateKey) params.emplace("private_key", entity.privateKey);
  if (entity.registerTime) params.emplace("register_time", entity.registerTime);
}

OrganizationMapper::OrganizationMapper() : WriteMapper("sys")
                                         , ReadMapper("sys") {}

OrganizationMapper::OrganizationMapper(std::shared_ptr<Database> database) : WriteMapper(database)
                                                                           , ReadMapper(database) {}

} // namespace alkaid
