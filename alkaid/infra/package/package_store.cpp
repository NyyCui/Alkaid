/** @file package_store.cpp 文件写入先于 SQLite 激活；中断不能覆盖当前可用版本。 */
#include "package_store.h"
#include "package_policy.h"
#include "safe_files.h"
#include "crypto.h"
#include "capability_policy.h"
#include <sqlite3.h>

namespace alkaid {
namespace _ {
class Statement {
public:
  Statement(sqlite3 *db, const char *sql) { if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK) value_ = nullptr; }
  ~Statement() { sqlite3_finalize(value_); }
  bool bind(int index, const std::string &value) { return value_ && sqlite3_bind_text(value_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK; }
  int step() { return value_ ? sqlite3_step(value_) : SQLITE_ERROR; }
  std::string text(int index) const { const auto value = sqlite3_column_text(value_, index); return value ? reinterpret_cast<const char *>(value) : ""; }
private:
  sqlite3_stmt *value_ = nullptr;
};
bool sql(sqlite3 *db, const char *text) { return sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK; }
RuntimeError database() { return {"REGISTRY_IO", "包注册表读写失败，已保留原有数据"}; }
bool digest(const std::string &value) { return value.size() == 64 && PackageCrypto::unhex(value).size() == 32; }
bool operation(sqlite3 *db, const std::string &id, const std::string &kind, const std::string &appId, const std::string &state) {
  Statement query(db, "INSERT INTO operations(id,kind,app_id,state) VALUES(?,?,?,?)");
  return query.bind(1, id) && query.bind(2, kind) && query.bind(3, appId) && query.bind(4, state) && query.step() == SQLITE_DONE;
}
bool phase(sqlite3 *db, const std::string &id, const std::string &state) {
  Statement query(db, "UPDATE operations SET state=? WHERE id=?");
  return query.bind(1, state) && query.bind(2, id) && query.step() == SQLITE_DONE;
}
}
PackageStore::~PackageStore() { if (db_) sqlite3_close(db_); if (lock_ != INVALID_HANDLE_VALUE) CloseHandle(lock_); }
std::optional<RuntimeError> PackageStore::initialize() {
  if (!PackageFiles::directory(root_ / "versions") || !PackageFiles::directory(root_ / "staging") || !PackageFiles::safe(root_ / "registry.sqlite"))
    return RuntimeError{"REGISTRY_IO", "应用目录不可写或包含重解析点"};
  for (const auto file : {"store.lock", "registry.sqlite-wal", "registry.sqlite-shm"})
    if (!PackageFiles::safe(root_ / file)) return RuntimeError{"REGISTRY_IO","注册表辅助文件包含重解析点"};
  lock_ = CreateFileW((root_ / "store.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (lock_ == INVALID_HANDLE_VALUE) return RuntimeError{"REGISTRY_BUSY","此数据目录已由其他运行时或管理工具使用"};
  const auto path = (root_ / "registry.sqlite").u8string();
  if (sqlite3_open_v2(reinterpret_cast<const char *>(path.c_str()), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) return _::database();
  sqlite3_busy_timeout(db_, 3000);
  if (!_::sql(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF;")) return _::database();
  _::Statement version(db_, "PRAGMA user_version");
  if (version.step() != SQLITE_ROW || (version.text(0) != "0" && version.text(0) != "1" && version.text(0) != "2")) return RuntimeError{"REGISTRY_VERSION", "注册表版本不受当前运行时支持"};
  const bool migrate = version.text(0) != "2";
  const auto kind = kind_ == Kind::Plugin ? "plugin" : "app";
  if (!_::sql(db_, "CREATE TABLE IF NOT EXISTS registry_kind(singleton INTEGER PRIMARY KEY CHECK(singleton=1),kind TEXT NOT NULL);"))
    return _::database();
  {
    _::Statement write(db_, "INSERT OR IGNORE INTO registry_kind VALUES(1,?)");
    if (!write.bind(1, migrate ? kind : "app") || write.step() != SQLITE_DONE) return _::database();
    _::Statement read(db_, "SELECT kind FROM registry_kind WHERE singleton=1");
    if (read.step() != SQLITE_ROW) return _::database();
    if (read.text(0) != kind) return RuntimeError{"REGISTRY_KIND", "包注册表类型与当前入口不匹配"};
  }
  if (!_::sql(db_, "BEGIN IMMEDIATE;"
      "CREATE TABLE IF NOT EXISTS publishers(key_id TEXT PRIMARY KEY,public_key TEXT NOT NULL,trusted INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS apps(app_id TEXT PRIMARY KEY,key_id TEXT NOT NULL,origin TEXT NOT NULL UNIQUE,name TEXT NOT NULL,version TEXT NOT NULL,active TEXT,previous TEXT);"
      "CREATE TABLE IF NOT EXISTS versions(digest TEXT PRIMARY KEY,app_id TEXT NOT NULL,key_id TEXT NOT NULL,version TEXT NOT NULL,UNIQUE(app_id,version));"
      "CREATE TABLE IF NOT EXISTS operations(id TEXT PRIMARY KEY,kind TEXT NOT NULL,app_id TEXT NOT NULL,state TEXT NOT NULL,created INTEGER NOT NULL DEFAULT(unixepoch()));"
      "COMMIT;")) { _::sql(db_, "ROLLBACK"); return _::database(); }
  if (migrate && !_::sql(db_, "BEGIN IMMEDIATE;ALTER TABLE apps ADD COLUMN grants TEXT NOT NULL DEFAULT '[]';PRAGMA user_version=2;COMMIT;")) {
    _::sql(db_, "ROLLBACK"); return _::database();
  }
  if (!_::sql(db_, "UPDATE operations SET state='interrupted' WHERE state IN ('prepared','ready')")) return _::database();
  clean();
  initialized_ = true;
  return std::nullopt;
}

RuntimeResult<std::shared_ptr<const VerifiedPackage>> PackageStore::loadVersion(const std::string &digest, const std::string &appId, const std::string &key) const {
  if (!_::digest(digest)) return RuntimeError{"PACKAGE_CORRUPT", "已登记版本摘要不合法"};
  const auto path = root_ / "versions" / digest / "package.ypkg";
  if (!PackageFiles::safe(path)) return RuntimeError{"PACKAGE_CORRUPT", "应用版本目录包含重解析点"};
  const auto loaded = PackageVerifier::read(path);
  if (const auto error = std::get_if<RuntimeError>(&loaded)) return *error;
  const auto package = std::get<std::shared_ptr<const VerifiedPackage>>(loaded);
  if (package->digest != digest || package->id() != appId || package->publisherKeyId != key ||
      (kind_ == Kind::Plugin) != !package->pluginId.empty())
    return RuntimeError{"PACKAGE_CORRUPT", "包内容与登记身份不一致"};
  _::Statement trusted(db_, "SELECT public_key FROM publishers WHERE key_id=? AND trusted=1");
  if (!trusted.bind(1, key) || trusted.step() != SQLITE_ROW || trusted.text(0) != package->publicKey)
    return RuntimeError{"PUBLISHER_UNTRUSTED", "应用发布者尚未受信任"};
  return package;
}

RuntimeResult<PackageSnapshot> PackageStore::load(bool recover) {
  struct Row { InstalledPackage app; std::string active, previous; };
  std::vector<Row> rows;
  {
    _::Statement query(db_, "SELECT app_id,name,version,key_id,origin,active,previous,grants FROM apps WHERE active IS NOT NULL ORDER BY app_id");
    int status;
    while ((status = query.step()) == SQLITE_ROW) {
      const auto grants = Json::parse(query.text(7), nullptr, false);
      if (!CapabilityPolicy::valid(grants,true)) return _::database();
      rows.push_back({{query.text(0),query.text(1),query.text(2),query.text(3),query.text(4),"",!query.text(6).empty(),nullptr,grants.get<std::set<std::string>>()},query.text(5),query.text(6)});
    }
    if (status != SQLITE_DONE) return _::database();
  }
  PackageSnapshot result;
  std::size_t memory = 0;
  for (auto &row : rows) {
    auto loaded = loadVersion(row.active, row.app.appId, row.app.publisherKeyId);
    if (std::holds_alternative<RuntimeError>(loaded) && recover && !row.previous.empty()) {
      auto old = loadVersion(row.previous, row.app.appId, row.app.publisherKeyId);
      if (const auto package = std::get_if<std::shared_ptr<const VerifiedPackage>>(&old)) {
        _::Statement repair(db_, "UPDATE apps SET active=previous,previous=NULL,name=?,version=? WHERE app_id=?");
        if (!repair.bind(1, (*package)->name) || !repair.bind(2, (*package)->version) || !repair.bind(3, row.app.appId) || repair.step() != SQLITE_DONE) return _::database();
        row.app.name = (*package)->name;
        row.app.version = (*package)->version;
        loaded = std::move(old);
        row.app.canRollback = false;
      }
    }
    if (const auto package = std::get_if<std::shared_ptr<const VerifiedPackage>>(&loaded)) {
      memory += (*package)->archive.size();
      for (const auto &[name, content] : (*package)->assets) memory += content.size();
      if (memory > 256 * 1024 * 1024) row.app.error = "已安装应用预加载总量超过当前 256 MiB 限制";
      else {
        row.app.package = *package;
        row.app.dependencyError = resolveDependencies(**package, row.app.pluginBindings);
        if (row.app.dependencyError) row.app.error = row.app.dependencyError->message;
      }
    } else row.app.error = std::get<RuntimeError>(loaded).message;
    result.push_back(std::move(row.app));
  }
  return result;
}

RuntimeResult<std::shared_ptr<const VerifiedPackage>> PackageStore::resolvePlugin(
    const std::string &id, const std::string &minimum, const std::string &maximum) const {
  const auto min = PackagePolicy::version(minimum);
  const auto max = PackagePolicy::version(maximum);
  if (kind_ != Kind::Plugin || !PackagePolicy::appId(id) || !min || !max || *min >= *max)
    return RuntimeError{"INVALID_REQUEST", "插件标识或版本范围不合法"};
  if (!initialized_) return RuntimeError{"PLUGIN_UNAVAILABLE", "插件注册表未就绪"};
  _::Statement query(db_, "SELECT active,previous,key_id FROM apps WHERE app_id=? AND active IS NOT NULL");
  if (!query.bind(1, id)) return _::database();
  const auto status = query.step();
  if (status != SQLITE_DONE && status != SQLITE_ROW) return _::database();
  std::shared_ptr<const VerifiedPackage> selected;
  if (status == SQLITE_ROW) {
    std::optional<std::array<int, 3>> selectedVersion;
    for (int column : {0, 1}) {
      const auto digest = query.text(column);
      if (digest.empty()) continue;
      const auto loaded = loadVersion(digest, id, query.text(2));
      const auto candidate = std::get_if<std::shared_ptr<const VerifiedPackage>>(&loaded);
      if (!candidate) continue;
      const auto version = PackagePolicy::version((*candidate)->version);
      if (version && *version >= *min && *version < *max && (!selectedVersion || *version > *selectedVersion)) {
        selected = *candidate;
        selectedVersion = version;
      }
    }
  }
  if (!selected) return RuntimeError{"PLUGIN_UNAVAILABLE", "没有满足范围的可信插件版本"};
  return selected;
}

std::optional<RuntimeError> PackageStore::resolveDependencies(const VerifiedPackage &package,
    std::map<std::string, PluginBinding> &bindings) const {
  for (const auto &dependency : package.dependencies) {
    const auto id = dependency["pluginId"].get<std::string>();
    if (plugins_) {
      const auto result = plugins_->resolvePlugin(id, dependency["minVersion"].get<std::string>(),
        dependency["maxVersionExclusive"].get<std::string>());
      if (const auto candidate = std::get_if<std::shared_ptr<const VerifiedPackage>>(&result)) {
        bindings.emplace(id, PluginBinding{(*candidate)->version, (*candidate)->digest, (*candidate)->publisherKeyId,
          (*candidate)->pluginEvents.get<std::set<std::string>>()});
        continue;
      }
    }
    if (dependency["required"].get<bool>())
      return RuntimeError{"PLUGIN_UNAVAILABLE", "缺少满足版本范围的可信必需插件：" + id};
  }
  return std::nullopt;
}

RuntimeResult<PackagePreview> PackageStore::preview(std::shared_ptr<const VerifiedPackage> package) {
  if ((kind_ == Kind::Plugin) != !package->pluginId.empty())
    return RuntimeError{"PACKAGE_TYPE_MISMATCH", "应用与插件必须通过各自的入口安装"};
  std::map<std::string, PluginBinding> bindings;
  if (const auto error = resolveDependencies(*package, bindings)) return *error;
  PackagePreview result{package, false, "", package->permissions};
  _::Statement trust(db_, "SELECT public_key,trusted FROM publishers WHERE key_id=?");
  if (!trust.bind(1, package->publisherKeyId)) return _::database();
  const auto trustStatus = trust.step();
  if (trustStatus == SQLITE_ROW) {
    if (trust.text(0) != package->publicKey || trust.text(1) != "1") return RuntimeError{"PUBLISHER_UNTRUSTED", "发布者公钥冲突或已撤销信任"};
    result.trusted = true;
  } else if (trustStatus != SQLITE_DONE) return _::database();
  _::Statement bound(db_, "SELECT key_id,version,active,grants FROM apps WHERE app_id=?");
  if (!bound.bind(1, package->id())) return _::database();
  const auto boundStatus = bound.step();
  if (boundStatus == SQLITE_ROW) {
    if (bound.text(0) != package->publisherKeyId) return RuntimeError{"PUBLISHER_MISMATCH", "包标识已绑定其他发布者"};
    if (!bound.text(2).empty()) {
      result.currentVersion = bound.text(1);
      const auto grants = Json::parse(bound.text(3), nullptr, false);
      if (!CapabilityPolicy::valid(grants,true)) return _::database();
      for (const auto &grant : grants) result.addedPermissions.erase(grant.get<std::string>());
    }
    const auto current = PackagePolicy::version(bound.text(1));
    if (!current || *PackagePolicy::version(package->version) < *current) return RuntimeError{"VERSION_DOWNGRADE", "不允许通过安装降级，请使用恢复上一版本"};
  } else if (boundStatus != SQLITE_DONE) return _::database();
  _::Statement prior(db_, "SELECT digest FROM versions WHERE app_id=? AND version=?");
  if (!prior.bind(1, package->id()) || !prior.bind(2, package->version)) return _::database();
  const auto priorStatus = prior.step();
  if (priorStatus == SQLITE_ROW && prior.text(0) != package->digest) return RuntimeError{"VERSION_CONFLICT", "同一应用版本不能对应不同包内容"};
  if (priorStatus != SQLITE_ROW && priorStatus != SQLITE_DONE) return _::database();
  _::Statement count(db_, "SELECT count(*) FROM apps WHERE active IS NOT NULL");
  if (count.step() != SQLITE_ROW) return _::database();
  const auto countText = count.text(0);
  int activeCount = 0;
  if (std::from_chars(countText.data(), countText.data() + countText.size(), activeCount).ec != std::errc{}) return _::database();
  if (result.currentVersion.empty() && activeCount >= 16) return RuntimeError{"RESOURCE_EXHAUSTED", "当前开发版最多激活 16 个安装应用"};
  return result;
}

std::optional<RuntimeError> PackageStore::install(const PackagePreview &candidate, const std::string &id, bool trustPublisher) {
  const auto checked = preview(candidate.package);
  if (const auto error = std::get_if<RuntimeError>(&checked)) return *error;
  const auto &preview = std::get<PackagePreview>(checked);
  const auto &package = *preview.package;
  if (!preview.trusted && !trustPublisher) return RuntimeError{"PUBLISHER_UNTRUSTED", "请先核对发布者指纹并明确确认信任"};
  if (!_::digest(id)) return RuntimeError{"INVALID_REQUEST", "安装操作标识不合法"};
  if (!_::operation(db_, id, "install", package.id(), "prepared")) return _::database();
  if (fault_) fault_("prepared");
  const auto staging = root_ / "staging" / id;
  const auto destination = root_ / "versions" / package.digest;
  std::error_code error;
  if (!PackageFiles::directory(staging) || !PackageFiles::write(staging / "package.ypkg", package.archive))
    return RuntimeError{"PACKAGE_IO", "暂存包写入失败，旧版本仍然有效"};
  if (fault_) fault_("staged");
  if (std::filesystem::exists(destination, error)) {
    const auto existing = PackageVerifier::read(destination / "package.ypkg");
    if (!PackageFiles::safe(destination) || !std::holds_alternative<std::shared_ptr<const VerifiedPackage>>(existing) ||
        std::get<std::shared_ptr<const VerifiedPackage>>(existing)->digest != package.digest) return RuntimeError{"PACKAGE_CORRUPT", "已有版本目录损坏，不能覆盖"};
    PackageFiles::removeTree(root_ / "staging", staging);
  } else if (error || !PackageFiles::safe(destination) || !MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
    return RuntimeError{"PACKAGE_IO", "版本目录提交失败，旧版本仍然有效"};
  if (!_::phase(db_, id, "ready")) return _::database();
  if (fault_) fault_("ready");
  if (!_::sql(db_, "BEGIN IMMEDIATE")) return _::database();
  _::Statement publisher(db_, "INSERT INTO publishers(key_id,public_key,trusted) VALUES(?,?,1) ON CONFLICT(key_id) DO NOTHING");
  _::Statement version(db_, "INSERT INTO versions(digest,app_id,key_id,version) VALUES(?,?,?,?) ON CONFLICT(digest) DO NOTHING");
  _::Statement app(db_, "INSERT INTO apps(app_id,key_id,origin,name,version,active,previous,grants) VALUES(?,?,?,?,?,?,NULL,?) "
    "ON CONFLICT(app_id) DO UPDATE SET previous=CASE WHEN active=excluded.active THEN previous ELSE active END,active=excluded.active,name=excluded.name,version=excluded.version,grants=excluded.grants");
  const auto origin = (kind_ == Kind::Plugin ? "p-" : "a-") + PackageCrypto::sha256(package.id()).substr(0, 32);
  const bool valid = publisher.bind(1, package.publisherKeyId) && publisher.bind(2, package.publicKey) && publisher.step() == SQLITE_DONE &&
    version.bind(1, package.digest) && version.bind(2, package.id()) && version.bind(3, package.publisherKeyId) && version.bind(4, package.version) && version.step() == SQLITE_DONE &&
    app.bind(1, package.id()) && app.bind(2, package.publisherKeyId) && app.bind(3, origin) && app.bind(4, package.name) && app.bind(5, package.version) && app.bind(6, package.digest) && app.bind(7, Json(package.permissions).dump()) && app.step() == SQLITE_DONE &&
    _::phase(db_, id, "committed");
  if (fault_) fault_("before-commit");
  if (!valid || !_::sql(db_, "COMMIT")) { _::sql(db_, "ROLLBACK"); return _::database(); }
  if (fault_) fault_("committed");
  clean();
  return std::nullopt;
}

std::optional<RuntimeError> PackageStore::setPermissions(const std::string &appId, const std::string &id, const Json &permissions) {
  if (kind_ != Kind::Application || !CapabilityPolicy::valid(permissions, true))
    return RuntimeError{"INVALID_REQUEST", "权限清单不合法"};
  _::Statement current(db_, "SELECT active,key_id FROM apps WHERE app_id=? AND active IS NOT NULL");
  if (!current.bind(1, appId)) return _::database();
  const auto status = current.step();
  if (status == SQLITE_DONE) return RuntimeError{"APP_NOT_INSTALLED", "应用尚未安装"};
  if (status != SQLITE_ROW) return _::database();
  const auto active = loadVersion(current.text(0), appId, current.text(1));
  if (const auto failure = std::get_if<RuntimeError>(&active)) return *failure;
  const auto &declared = std::get<std::shared_ptr<const VerifiedPackage>>(active)->permissions;
  const auto selected = permissions.get<std::set<std::string>>();
  for (const auto &permission : selected) {
    if (!declared.contains(permission))
      return RuntimeError{"PERMISSION_DENIED", "当前签名包未声明此能力"};
    if (permission.starts_with("net.endpoint:") && !selected.contains("net.request"))
      return RuntimeError{"INVALID_REQUEST", "网络端点授权需要同时启用网络访问"};
  }
  if (!_::sql(db_, "BEGIN IMMEDIATE")) return _::database();
  _::Statement update(db_, "UPDATE apps SET grants=? WHERE app_id=?");
  if (!update.bind(1, Json(selected).dump()) || !update.bind(2, appId) || update.step() != SQLITE_DONE ||
      !_::operation(db_, id, "permissions", appId, "committed") || !_::sql(db_, "COMMIT")) {
    _::sql(db_, "ROLLBACK");
    return _::database();
  }
  return std::nullopt;
}

std::optional<RuntimeError> PackageStore::uninstall(const std::string &appId, const std::string &id) {
  _::Statement current(db_, "SELECT active FROM apps WHERE app_id=?");
  if (!current.bind(1, appId) || current.step() != SQLITE_ROW || current.text(0).empty()) return RuntimeError{"APP_NOT_INSTALLED", "应用尚未安装"};
  if (!_::sql(db_, "BEGIN IMMEDIATE")) return _::database();
  _::Statement remove(db_, "UPDATE apps SET active=NULL,previous=NULL WHERE app_id=?");
  if (!remove.bind(1, appId) || remove.step() != SQLITE_DONE || !_::operation(db_, id, "uninstall", appId, "committed") || !_::sql(db_, "COMMIT")) {
    _::sql(db_, "ROLLBACK"); return _::database();
  }
  clean();
  return std::nullopt;
}

std::optional<RuntimeError> PackageStore::rollback(const std::string &appId, const std::string &id) {
  _::Statement current(db_, "SELECT active,previous,key_id,grants FROM apps WHERE app_id=?");
  if (!current.bind(1, appId) || current.step() != SQLITE_ROW || current.text(0).empty() || current.text(1).empty()) return RuntimeError{"NO_PREVIOUS_VERSION", "没有可恢复的上一版本"};
  const auto previous = loadVersion(current.text(1), appId, current.text(2));
  if (const auto error = std::get_if<RuntimeError>(&previous)) return *error;
  const auto package = std::get<std::shared_ptr<const VerifiedPackage>>(previous);
  std::map<std::string, PluginBinding> bindings;
  if (const auto error = resolveDependencies(*package, bindings)) return *error;
  const auto grants = Json::parse(current.text(3), nullptr, false);
  if (!CapabilityPolicy::valid(grants,true)) return _::database();
  const auto granted = grants.get<std::set<std::string>>();
  for (const auto &permission : package->permissions) if (!granted.contains(permission))
    return RuntimeError{"PERMISSION_DENIED", "上一版本要求当前未授权的能力，请通过安装确认重新授权"};
  if (!_::sql(db_, "BEGIN IMMEDIATE")) return _::database();
  _::Statement change(db_, "UPDATE apps SET active=previous,previous=active,name=?,version=? WHERE app_id=?");
  if (!change.bind(1, package->name) || !change.bind(2, package->version) || !change.bind(3, appId) || change.step() != SQLITE_DONE ||
      !_::operation(db_, id, "rollback", appId, "committed") || !_::sql(db_, "COMMIT")) { _::sql(db_, "ROLLBACK"); return _::database(); }
  return std::nullopt;
}

Json PackageStore::history() const {
  Json records = Json::array();
  _::Statement query(db_, "SELECT id,kind,app_id,state FROM operations ORDER BY created DESC,rowid DESC LIMIT 32");
  while (query.step() == SQLITE_ROW) {
    const auto state = query.text(3);
    records.push_back({{"taskId",query.text(0)}, {"kind",query.text(1)}, {"appId",query.text(2)},
      {"status",state == "committed" ? "succeeded" : "failed"}, {"phase",state},
      {"message",state == "committed" ? "操作已完成" : "上次操作中断，未激活新版本"}});
  }
  return records;
}

void PackageStore::clean() {
  std::set<std::string> referenced;
  _::Statement query(db_, "SELECT active,previous FROM apps");
  int status;
  while ((status = query.step()) == SQLITE_ROW) { referenced.insert(query.text(0)); referenced.insert(query.text(1)); }
  if (status != SQLITE_DONE) return;
  for (const auto group : {"staging", "versions"}) {
    const auto base = root_ / group;
    if (!PackageFiles::safe(base)) continue;
    std::error_code error;
    for (std::filesystem::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
      const auto name = it->path().filename().string();
      if (_::digest(name) && (std::string(group) == "staging" || !referenced.contains(name))) PackageFiles::removeTree(base, it->path());
    }
  }
}
} // namespace alkaid
