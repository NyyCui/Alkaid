/** @file data_store.cpp 有界事务存储、UTF-8 文本和原生选择产生的会话授权。 */
#include "data_store.h"
#include "package_policy.h"
#include "safe_files.h"
#include "text_codec.h"
#include <sqlite3.h>
namespace alkaid {
namespace _ {
RuntimeError io() { return {"STORAGE_IO", "私有数据读写失败，事务未完成"}; }
RuntimeError expired() { return {"REQUEST_EXPIRED", "请求已取消、超时或页面已经关闭"}; }
class Query {
public:
  Query(sqlite3 *db, const char *sql) { sqlite3_prepare_v2(db, sql, -1, &value_, nullptr); }
  ~Query() { sqlite3_finalize(value_); }
  bool bind(int index, const std::string &text) { return value_ && sqlite3_bind_text(value_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) == SQLITE_OK; }
  int step() { return value_ ? sqlite3_step(value_) : SQLITE_ERROR; }
  std::string text(int index) const { const auto data = sqlite3_column_text(value_, index); return data ? std::string(reinterpret_cast<const char *>(data), sqlite3_column_bytes(value_, index)) : ""; }
  int number(int index) const { return sqlite3_column_int(value_, index); }
private:
  sqlite3_stmt *value_ = nullptr;
};
static bool sql(sqlite3 *db, const char *text) { return sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK; }
bool key(const Json &params, const char *name) {
  if (!params.contains(name) || !params[name].is_string()) return false;
  const auto value = params[name].get<std::string>();
  return value.size() <= 80 && value.find('/') == std::string::npos && PackagePolicy::path(value);
}
bool text(const Json &params) {
  return params.contains("text") && params["text"].is_string() && params["text"].get_ref<const std::string &>().size() <= DataStore::MAX_TEXT;
}
bool handle(const Json &params) {
  return params.contains("handle") && params["handle"].is_string() && params["handle"].get_ref<const std::string &>().size() == 64 &&
    PackageCrypto::unhex(params["handle"].get<std::string>()).size() == 32;
}
bool value(const Json &params) { return params.contains("value") && params["value"].dump().size() <= 64 * 1024; }
bool operation(const Json &params) {
  return params.is_object() && params.contains("type") &&
    ((params["type"] == "set" && PackagePolicy::keys(params,{"type","key","value"}) && key(params,"key") && value(params)) ||
     (params["type"] == "remove" && PackagePolicy::keys(params,{"type","key"}) && key(params,"key")));
}
}
DataStore::~DataStore() { while (!streams_.empty()) releaseStream(streams_.begin()->first); if (db_) sqlite3_close(db_); }
bool DataStore::validParams(const std::string &method, const Json &params) {
  const auto token=[&](const char *name) { return params.contains(name) && params[name].is_string() && params[name].get_ref<const std::string &>().size()==64 && PackageCrypto::unhex(params[name].get<std::string>()).size()==32; };
  if (method == "files.beginRead" || method == "files.beginWrite" || method == "files.beginPrivateRead" || method == "files.beginPrivateWrite") {
    const bool privateFile=method.find("Private")!=std::string::npos;
    return PackagePolicy::keys(params,{privateFile?"name":"handle","encoding"}) && (privateFile?_::key(params,"name"):token("handle")) &&
      params["encoding"].is_string() && TextCodec::encoding(params["encoding"].get<std::string>(),method.ends_with("Read"));
  }
  if (method == "files.readNext" || method == "files.commitWrite" || method == "files.abortStream") return PackagePolicy::keys(params,{"streamId"}) && token("streamId");
  if (method == "files.appendChunk") {
    const bool text=params.contains("text"); const char *name=text?"text":"data";
    return PackagePolicy::keys(params,{"streamId","index",name}) && token("streamId") && params["index"].is_number_integer() &&
      params["index"].get<int64_t>()>=0 && params["index"].get<int64_t>()<=9007199254740991LL && params[name].is_string() &&
      params[name].get_ref<const std::string &>().size()<=(text?CHUNK_BYTES:((CHUNK_BYTES+2)/3*4));
  }
  if (method == "storage.get" || method == "storage.remove") return PackagePolicy::keys(params,{"key"}) && _::key(params,"key");
  if (method == "storage.set") return PackagePolicy::keys(params,{"key","value"}) && _::key(params,"key") && _::value(params);
  if (method == "storage.batch") return PackagePolicy::keys(params,{"operations"}) && params["operations"].is_array() &&
    !params["operations"].empty() && params["operations"].size() <= 64 && std::ranges::all_of(params["operations"],_::operation);
  if (method == "files.readPrivateText" || method == "files.removePrivate") return PackagePolicy::keys(params,{"name"}) && _::key(params,"name");
  if (method == "files.writePrivateText") return PackagePolicy::keys(params,{"name","text"}) && _::key(params,"name") && _::text(params);
  if (method == "dialog.openFile" || method == "dialog.saveFile") return PackagePolicy::keys(params,{});
  if (method == "files.readText" || method == "files.close") return PackagePolicy::keys(params,{"handle"}) && _::handle(params);
  if (method == "files.writeText") return PackagePolicy::keys(params,{"handle","text"}) && _::handle(params) && _::text(params);
  return false;
}
std::optional<RuntimeError> DataStore::initialize() {
  if (db_) return std::nullopt;
  std::vector<FileAccess::Handle> pins;
  if (!PackageFiles::directory(root_) || !FileAccess::pin(root_, pins)) return _::io();
  for (const auto *name : {"content.sqlite", "content.sqlite-wal", "content.sqlite-shm", "content.sqlite-journal"})
    if (!PackageFiles::safe(root_ / name)) return _::io();
  const auto path = (root_ / "content.sqlite").u8string();
  sqlite3 *opened = nullptr;
  if (sqlite3_open_v2(reinterpret_cast<const char *>(path.c_str()), &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
    if (opened) sqlite3_close(opened); return _::io();
  }
  sqlite3_busy_timeout(opened, 1000);
  if (config_) {
    std::string version;
    { _::Query query(opened, "PRAGMA user_version"); if (query.step() == SQLITE_ROW) version = query.text(0); }
    if (version != "0" && version != "1") {
      sqlite3_close(opened);
      return RuntimeError{"CONFIG_SCHEMA_UNSUPPORTED", "配置库版本不受当前运行时支持"};
    }
  }
  if (!_::sql(opened, "PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;PRAGMA trusted_schema=OFF;"
      "CREATE TABLE IF NOT EXISTS content(kind TEXT NOT NULL,name TEXT NOT NULL,value TEXT NOT NULL,revision TEXT NOT NULL DEFAULT '',PRIMARY KEY(kind,name));")) {
    sqlite3_close(opened); return _::io();
  }
  bool hasRevision=false;
  { _::Query columns(opened,"PRAGMA table_info(content)"); while (columns.step()==SQLITE_ROW) if (columns.text(1)=="revision") hasRevision=true; }
  if (!hasRevision && !_::sql(opened,"ALTER TABLE content ADD COLUMN revision TEXT NOT NULL DEFAULT '';")) { sqlite3_close(opened); return _::io(); }
  if (config_ && !_::sql(opened, "PRAGMA user_version=1")) { sqlite3_close(opened); return _::io(); }
  // 仅清理本应用上次中断的随机暂存文件，仍被其他句柄占用的文件不会被删除。
  std::error_code cleanupError;
  for (std::filesystem::directory_iterator it(root_,cleanupError),end;!cleanupError && it!=end;it.increment(cleanupError)) {
    const auto name=it->path().filename().string(); const std::string prefix=".alkaid-transfer-";
    if (name.starts_with(prefix) && name.ends_with(".tmp") && name.size()==prefix.size()+68 &&
        PackageCrypto::unhex(name.substr(prefix.size(),64)).size()==32) DeleteFileW(it->path().c_str());
  }
  pins_ = std::move(pins);
  db_ = opened; return std::nullopt;
}
RuntimeResult<Json> DataStore::privateCall(const std::string &method, const Json &params, const DataLease &lease) {
  if (const auto error = initialize()) return *error;
  const bool kv = method.starts_with("storage.");
  const std::string kind = kv ? "kv" : "file";
  const std::string name = params.value(kv ? "key" : "name", "");
  if (method == "storage.get" || method == "files.readPrivateText") {
    _::Query query(db_, "SELECT value FROM content WHERE kind=? AND name=?");
    if (!query.bind(1,kind) || !query.bind(2,name)) return _::io();
    const auto status = query.step();
    if (status == SQLITE_DONE) return kv ? Json{{"found",false},{"value",nullptr}} : Json(nullptr);
    if (status != SQLITE_ROW) return _::io();
    const auto bytes = query.text(0);
    if (!kv && bytes.size()>MAX_TEXT) return RuntimeError{"FILE_TOO_LARGE","请使用新版 SDK 的分块私有文本接口读取此文件"};
    if (!kv) return Json(bytes);
    const auto value = Json::parse(bytes,nullptr,false);
    if (value.is_discarded()) return _::io();
    return Json{{"found",true},{"value",value}};
  }
  Json operations = Json::array();
  if (method == "storage.batch") operations = params["operations"];
  else if (method == "storage.remove" || method == "files.removePrivate") operations.push_back({{"type","remove"},{"key",name}});
  else operations.push_back({{"type","set"},{"key",name},{"value",kv ? params["value"] : params["text"]}});
  if (!_::sql(db_, "BEGIN IMMEDIATE")) return _::io();
  const auto abort = [&](RuntimeError error) -> RuntimeResult<Json> { _::sql(db_, "ROLLBACK"); return error; };
  for (const auto &operation : operations) {
    const bool remove = operation["type"] == "remove";
    _::Query query(db_, remove ? "DELETE FROM content WHERE kind=? AND name=?" :
      "INSERT INTO content(kind,name,value,revision) VALUES(?,?,?,lower(hex(randomblob(16)))) ON CONFLICT(kind,name) DO UPDATE SET value=excluded.value,revision=excluded.revision");
    if (!query.bind(1,kind) || !query.bind(2,operation["key"].get<std::string>()) ||
        (!remove && !query.bind(3, kv ? operation["value"].dump() : operation["value"].get<std::string>())) || query.step() != SQLITE_DONE) return abort(_::io());
  }
  {
    _::Query quota(db_, "SELECT count(*),coalesce(sum(length(CAST(name AS BLOB))+length(CAST(value AS BLOB))),0) FROM content WHERE kind=?");
    if (!quota.bind(1,kind) || quota.step() != SQLITE_ROW) return abort(_::io());
    if (quota.number(0) > 128 || quota.number(1) > (kv ? 1024 * 1024 : 128 * 1024 * 1024))
      return abort({"QUOTA_EXCEEDED", "应用私有数据超过条目数或容量限制，整批操作已撤销"});
  }
  if (!lease.valid()) return abort(_::expired());
  if (!_::sql(db_, "COMMIT")) return abort(_::io());
  return Json(nullptr);
}
RuntimeResult<Json> DataStore::grant(const std::filesystem::path &path, bool writable, uint64_t session, const DataLease &lease) {
  if (!lease.valid()) return _::expired();
  if (handles_.size() >= 16) return RuntimeError{"RESOURCE_EXHAUSTED", "一个应用最多保留 16 个文件句柄，请关闭不再使用的文件"};
  const auto filename = path.filename().wstring();
  if (filename.empty() || filename == L"." || filename == L".." || filename.find(L':') != std::wstring::npos ||
      filename.back() == L'.' || filename.back() == L' ') return RuntimeError{"FILE_UNSUPPORTED", "不支持此文件名"};
  Grant grant{session,writable,path,{},FileAccess::Handle{},""};
  if (!FileAccess::pin(path.parent_path(), grant.pins)) return RuntimeError{"FILE_UNSUPPORTED", "请选择本地固定磁盘中的普通文件，不能使用目录链接或网络路径"};
  grant.file = FileAccess::open(path);
  const auto openError = GetLastError();
  if (!grant.file && (!writable || openError != ERROR_FILE_NOT_FOUND)) return RuntimeError{"FILE_BUSY", "文件无法打开，可能被占用或没有访问权限"};
  if (grant.file && !FileAccess::regular(grant.file.get())) return RuntimeError{"FILE_UNSUPPORTED", "不支持目录、重解析点或多硬链接文件"};
  if (grant.file && !GetFileInformationByHandle(grant.file.get(),&grant.snapshot)) return RuntimeError{"FILE_IO","无法取得文件快照"};
  const auto id = FileAccess::randomId();
  if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED", "无法分配文件句柄"};
  if (!lease.valid()) return _::expired();
  const auto name = path.filename().u8string();
  handles_.emplace(id,std::move(grant));
  return Json{{"handle",id},{"name",std::string(name.begin(),name.end())},{"writable",writable}};
}
RuntimeResult<Json> DataStore::read(Grant &grant) {
  if (!grant.file) return Json("");
  LARGE_INTEGER size{}, beginning{};
  if (!GetFileSizeEx(grant.file.get(), &size) || size.QuadPart < 0 || size.QuadPart > MAX_TEXT)
    return RuntimeError{"FILE_TOO_LARGE", "当前文本读写最多支持 128 KiB"};
  std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');
  DWORD count = 0;
  if (!SetFilePointerEx(grant.file.get(),beginning,nullptr,FILE_BEGIN) ||
      !ReadFile(grant.file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count != bytes.size())
    return RuntimeError{"FILE_IO", "读取文件失败"};
  if (!FileAccess::utf8(bytes) || bytes.find('\0') != std::string::npos) return RuntimeError{"FILE_ENCODING", "仅支持 UTF-8 文本，不能读取二进制或其他编码"};
  return Json(bytes);
}
RuntimeResult<Json> DataStore::write(Grant &grant, const std::string &text, const DataLease &lease) {
  if (!grant.writable) return RuntimeError{"PERMISSION_DENIED", "此句柄仅授权读取，请使用另存为选择保存目标"};
  if (!FileAccess::utf8(text) || text.find('\0') != std::string::npos) return RuntimeError{"FILE_ENCODING", "仅支持不含空字符的 UTF-8 文本"};
  if (const auto error=unchanged(grant)) return *error;
  auto current = FileAccess::open(grant.path);
  const auto openError = GetLastError();
  if ((grant.file && (!current || !FileAccess::regular(current.get()) || !FileAccess::same(current.get(),grant.file.get()))) ||
      (!grant.file && (current || openError != ERROR_FILE_NOT_FOUND)))
    return RuntimeError{"FILE_CHANGED", "保存目标已被替换或占用，请重新选择保存位置"};
  current.reset();
  if (grant.file && !grant.contentHash.empty()) {
    const auto content = read(grant);
    if (const auto error = std::get_if<RuntimeError>(&content)) return *error;
    if (PackageCrypto::sha256(std::get<Json>(content).get<std::string>()) != grant.contentHash)
      return RuntimeError{"FILE_CHANGED", "文件内容已发生变化，请重新选择保存位置"};
  }
  const auto id = FileAccess::randomId();
  if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED", "无法创建安全暂存文件"};
  const auto temporary = grant.path.parent_path() / (".alkaid-" + id + ".tmp");
  FileAccess::Handle output(CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!output) return RuntimeError{"FILE_IO", "无法在保存目录创建暂存文件"};
  const auto fail = [&](RuntimeError error) -> RuntimeResult<Json> { output.reset(); DeleteFileW(temporary.c_str()); return error; };
  DWORD written = 0;
  if (!WriteFile(output.get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr) || written != text.size() || !FlushFileBuffers(output.get()))
    return fail({"FILE_IO", "暂存写入失败，原文件未被截断"});
  if (!lease.valid()) return fail(_::expired());
  output.reset(); // ReplaceFile 独占打开暂存文件，提交前必须释放写句柄。
  const auto backup = grant.path.parent_path() / (".alkaid-" + id + ".bak");
  // 同目录替换保留旧文件直至提交；已有文件用 ReplaceFile 保留原 ACL 等元数据。
  const bool committed = grant.file ? ReplaceFileW(grant.path.c_str(),temporary.c_str(),backup.c_str(),0,nullptr,nullptr) != 0 :
    MoveFileExW(temporary.c_str(),grant.path.c_str(),MOVEFILE_WRITE_THROUGH) != 0;
  if (!committed) {
    const auto code = GetLastError();
    if (code == ERROR_UNABLE_TO_MOVE_REPLACEMENT || code == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
      grant.writable = false;
      return RuntimeError{"FILE_RECOVERY_REQUIRED", "文件替换中断，已保留同目录 .alkaid-" + id + ".bak / .tmp 恢复文件，请检查保存目录"};
    }
    return fail({"FILE_IO", "文件替换失败，请检查文件是否被占用并重新选择目标"});
  }
  DeleteFileW(backup.c_str());
  grant.file = FileAccess::open(grant.path);
  grant.contentHash = PackageCrypto::sha256(text);
  if (grant.file) GetFileInformationByHandle(grant.file.get(),&grant.snapshot);
  if (!grant.file || !FileAccess::regular(grant.file.get())) {
    grant.writable = false;
    return RuntimeError{"FILE_REOPEN_FAILED", "内容已保存，但无法继续持有文件，请重新打开"};
  }
  return Json(nullptr);
}
RuntimeResult<Json> DataStore::call(const std::string &method, const Json &params, uint64_t session, const DataLease &lease) {
  if (!validParams(method,params)) return RuntimeError{"INVALID_REQUEST", "存储或文件参数不合法，文本最多 128 KiB，键和值受配额限制"};
  if (!lease.valid()) return _::expired();
  if (method.starts_with("dialog.")) return RuntimeError{"INVALID_REQUEST", "文件授权必须来自原生选择器"};
  if (method.starts_with("files.begin") || method == "files.readNext" || method == "files.appendChunk" || method == "files.commitWrite" || method == "files.abortStream")
    return streamCall(method,params,session,lease);
  if (method.starts_with("storage.") || method == "files.readPrivateText" || method == "files.writePrivateText" || method == "files.removePrivate")
    return privateCall(method,params,lease);
  const auto id = params["handle"].get<std::string>();
  const auto found = handles_.find(id);
  if (found == handles_.end() || found->second.session != session) return RuntimeError{"HANDLE_EXPIRED", "文件句柄不存在或不属于当前页面"};
  if (method == "files.close") { release(id); return Json(nullptr); }
  if (method == "files.readText") return read(found->second);
  return write(found->second,params["text"].get<std::string>(),lease);
}
void DataStore::revoke(uint64_t session) {
  std::vector<std::string> expired;
  for (const auto &[id,stream]:streams_) if (stream.session==session) expired.push_back(id);
  for (const auto &id:expired) releaseStream(id);
  std::erase_if(handles_,[session](const auto &item) { return item.second.session==session; });
}
}
