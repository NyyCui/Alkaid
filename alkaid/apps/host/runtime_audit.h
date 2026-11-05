/** @file runtime_audit.h 记录固定字段的本机诊断事件，避免写入调用载荷。 */
#ifndef ALKAID_RUNTIME_AUDIT_H
#define ALKAID_RUNTIME_AUDIT_H
#include "safe_files.h"
#include <sqlite3.h>

namespace alkaid::RuntimeAudit {
struct Database {
  sqlite3 *db = nullptr;
  explicit Database(const std::filesystem::path &root) {
    const auto file = root / "config" / "audit.sqlite";
    if (!PackageFiles::directory(file.parent_path()) || !PackageFiles::safe(file)) return;
    const auto path = file.u8string();
    if (sqlite3_open_v2(reinterpret_cast<const char *>(path.c_str()), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK ||
        sqlite3_busy_timeout(db, 250) != SQLITE_OK ||
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS audit(id INTEGER PRIMARY KEY, time INTEGER NOT NULL,"
          "operation TEXT NOT NULL, app_id TEXT NOT NULL, target TEXT NOT NULL, code TEXT NOT NULL)", nullptr, nullptr, nullptr) != SQLITE_OK) {
      sqlite3_close(db);
      db = nullptr;
    }
  }
  ~Database() { sqlite3_close(db); }
};

inline void record(const std::filesystem::path &root, const std::string &operation,
    const std::string &appId, const std::string &target, const std::string &code) {
  const Database database(root);
  if (!database.db || sqlite3_exec(database.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return;
  sqlite3_stmt *statement = nullptr;
  const bool inserted = sqlite3_prepare_v2(database.db,
    "INSERT INTO audit(time,operation,app_id,target,code) VALUES(unixepoch(),?,?,?,?)", -1, &statement, nullptr) == SQLITE_OK &&
    sqlite3_bind_text(statement, 1, operation.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
    sqlite3_bind_text(statement, 2, appId.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
    sqlite3_bind_text(statement, 3, target.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
    sqlite3_bind_text(statement, 4, code.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
    sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  if (inserted && sqlite3_exec(database.db,
      "DELETE FROM audit WHERE id <= (SELECT id FROM audit ORDER BY id DESC LIMIT 1 OFFSET 512);COMMIT",
      nullptr, nullptr, nullptr) == SQLITE_OK) return;
  sqlite3_exec(database.db, "ROLLBACK", nullptr, nullptr, nullptr);
}

inline RuntimeResult<Json> list(const std::filesystem::path &root) {
  const Database database(root);
  const RuntimeError failure{"AUDIT_UNAVAILABLE", "无法读取本机诊断记录"};
  if (!database.db) return failure;
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(database.db,
      "SELECT time,operation,app_id,target,code FROM audit ORDER BY id DESC LIMIT 100", -1, &statement, nullptr) != SQLITE_OK)
    return failure;
  auto result = Json::array();
  const auto column = [&](int index) { return reinterpret_cast<const char *>(sqlite3_column_text(statement, index)); };
  int status;
  while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
    result.push_back({{"time",sqlite3_column_int64(statement, 0)},{"operation",column(1)},
      {"appId",column(2)},{"target",column(3)},{"code",column(4)}});
  }
  sqlite3_finalize(statement);
  if (status != SQLITE_DONE) return failure;
  return result;
}
}
#endif
