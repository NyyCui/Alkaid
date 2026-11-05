/** @file data_streams.cpp 有界字节/文本流、暂存写入和私有数据事务提交。 */
#include "data_store.h"
#include "text_codec.h"
#include <sqlite3.h>
namespace alkaid {
namespace stream_detail {
class Query {
public:
  Query(sqlite3 *db,const char *sql) { sqlite3_prepare_v2(db,sql,-1,&value_,nullptr); }
  ~Query() { sqlite3_finalize(value_); }
  bool text(int i,const std::string &value) { return value_ && sqlite3_bind_text(value_,i,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT)==SQLITE_OK; }
  bool number(int i,int64_t value) { return value_ && sqlite3_bind_int64(value_,i,value)==SQLITE_OK; }
  int step() { return value_?sqlite3_step(value_):SQLITE_ERROR; }
  int64_t number(int i) { return sqlite3_column_int64(value_,i); }
  std::string bytes(int i) { const auto ptr=sqlite3_column_blob(value_,i); return ptr?std::string(static_cast<const char *>(ptr),sqlite3_column_bytes(value_,i)):""; }
private:
  sqlite3_stmt *value_=nullptr;
};
RuntimeError io() { return {"FILE_IO","文件流读写失败，未提交的写入不会替换原内容"}; }
RuntimeError changed() { return {"FILE_CHANGED","文件已变化，请重新读取或选择保存目标"}; }
bool sql(sqlite3 *db,const char *statement) { return sqlite3_exec(db,statement,nullptr,nullptr,nullptr)==SQLITE_OK; }
bool seek(HANDLE file,uint64_t offset) { LARGE_INTEGER position{}; position.QuadPart=static_cast<LONGLONG>(offset); return SetFilePointerEx(file,position,nullptr,FILE_BEGIN)!=0; }
}
std::optional<RuntimeError> DataStore::unchanged(Grant &grant) {
  auto current=FileAccess::open(grant.path); const auto failure=GetLastError();
  if (!grant.file) return (!current && failure==ERROR_FILE_NOT_FOUND)?std::nullopt:std::optional<RuntimeError>(stream_detail::changed());
  BY_HANDLE_FILE_INFORMATION info{};
  if (!current || !FileAccess::regular(current.get()) || !FileAccess::same(current.get(),grant.file.get()) || !GetFileInformationByHandle(current.get(),&info) ||
      info.nFileSizeHigh!=grant.snapshot.nFileSizeHigh || info.nFileSizeLow!=grant.snapshot.nFileSizeLow ||
      CompareFileTime(&info.ftLastWriteTime,&grant.snapshot.ftLastWriteTime)!=0) return stream_detail::changed();
  return std::nullopt;
}
void DataStore::releaseStream(const std::string &id) {
  const auto found=streams_.find(id); if (found==streams_.end()) return;
  found->second.file.reset();
  if (!found->second.temporary.empty()) DeleteFileW(found->second.temporary.c_str());
  streams_.erase(found);
}
void DataStore::expireStreams() {
  std::vector<std::string> expired;
  for (const auto &[id,stream]:streams_) if (RuntimeClock::now()-stream.touched>std::chrono::minutes(5)) expired.push_back(id);
  for (const auto &id:expired) releaseStream(id);
}
void DataStore::release(const std::string &handle) {
  std::vector<std::string> expired;
  for (const auto &[id,stream]:streams_) if (stream.handle==handle) expired.push_back(id);
  for (const auto &id:expired) releaseStream(id);
  handles_.erase(handle);
}
RuntimeResult<Json> DataStore::privateSnapshot(const std::string &name) {
  stream_detail::Query query(db_,"SELECT rowid,length(CAST(value AS BLOB)),revision FROM content WHERE kind='file' AND name=?");
  if (!query.text(1,name)) return stream_detail::io();
  const auto status=query.step();
  if (status==SQLITE_DONE) return Json(nullptr);
  if (status!=SQLITE_ROW) return stream_detail::io();
  return Json{{"rowid",query.number(0)},{"size",query.number(1)},{"revision",query.bytes(2)}};
}
RuntimeResult<Json> DataStore::beginStream(const std::string &method,const Json &params,uint64_t session,const DataLease &lease) {
  expireStreams();
  if (streams_.size()>=8) return RuntimeError{"RESOURCE_EXHAUSTED","一个应用最多保留 8 个文件流，请结束不再使用的流"};
  Stream stream; stream.session=session; stream.writing=method.ends_with("Write"); stream.privateFile=method.find("Private")!=std::string::npos;
  stream.encoding=params["encoding"].get<std::string>();
  if (stream.privateFile) {
    if (const auto error=initialize()) return *error;
    stream.name=params["name"].get<std::string>();
    const auto snapshot=privateSnapshot(stream.name);
    if (const auto error=std::get_if<RuntimeError>(&snapshot)) return *error;
    const auto &value=std::get<Json>(snapshot);
    if (value.is_null() && !stream.writing) return Json(nullptr);
    stream.revision=value.is_null()?"missing":value.dump();
    if (!value.is_null()) stream.size=value["size"].get<uint64_t>();
  } else {
    stream.handle=params["handle"].get<std::string>();
    const auto found=handles_.find(stream.handle);
    if (found==handles_.end() || found->second.session!=session) return RuntimeError{"HANDLE_EXPIRED","文件句柄不属于当前页面或已经失效"};
    auto &grant=found->second;
    if (stream.writing && !grant.writable) return RuntimeError{"PERMISSION_DENIED","此句柄仅允许读取，请通过保存对话框确认写入目标"};
    if (const auto error=unchanged(grant)) return *error;
    stream.size=(static_cast<uint64_t>(grant.snapshot.nFileSizeHigh)<<32)|grant.snapshot.nFileSizeLow;
    if (!stream.writing && grant.file) {
      HANDLE duplicate=INVALID_HANDLE_VALUE;
      if (!DuplicateHandle(GetCurrentProcess(),grant.file.get(),GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)) return stream_detail::io();
      stream.file=FileAccess::Handle(duplicate);
    }
  }
  const auto id=FileAccess::randomId(); if (id.empty()) return stream_detail::io();
  if (stream.writing) {
    std::size_t writers=0;
    for (const auto &[key,active]:streams_) if (active.writing) {
      ++writers;
      if ((stream.privateFile && active.privateFile && active.name==stream.name) || (!stream.privateFile && active.handle==stream.handle))
        return RuntimeError{"FILE_BUSY","这个目标已有写入事务，请先完成或取消"};
    }
    if (writers>=4) return RuntimeError{"RESOURCE_EXHAUSTED","一个应用最多同时暂存 4 次文件写入"};
    const auto directory=stream.privateFile?root_:handles_.at(stream.handle).path.parent_path();
    stream.temporary=directory/(".alkaid-transfer-"+id+".tmp");
    stream.file=FileAccess::Handle(CreateFileW(stream.temporary.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if (!stream.file) return stream_detail::io();
    stream.size=0;
  }
  const auto size=stream.size;
  streams_.emplace(id,std::move(stream));
  if (!lease.valid()) { releaseStream(id); return RuntimeError{"REQUEST_EXPIRED","文件流创建请求已经失效"}; }
  return Json{{"streamId",id},{"totalBytes",size},{"encoding",params["encoding"]}};
}
RuntimeResult<Json> DataStore::readStream(Stream &stream) {
  if (stream.writing) return RuntimeError{"INVALID_REQUEST","不能从写入事务读取数据"};
  const auto length=static_cast<std::size_t>(std::min<uint64_t>(CHUNK_BYTES,stream.size-stream.offset));
  std::string bytes(length,'\0');
  if (stream.privateFile) {
    const auto snapshot=privateSnapshot(stream.name);
    if (const auto error=std::get_if<RuntimeError>(&snapshot)) return *error;
    if (std::get<Json>(snapshot).dump()!=stream.revision) return stream_detail::changed();
    // 增量 BLOB 读取避免 substr 在每一块上物化整份大草稿；旧 TEXT 行也可只读打开。
    sqlite3_blob *blob=nullptr;
    if (sqlite3_blob_open(db_,"main","content","value",std::get<Json>(snapshot)["rowid"].get<int64_t>(),0,&blob)!=SQLITE_OK) return stream_detail::io();
    const bool read=length==0 || sqlite3_blob_read(blob,bytes.data(),static_cast<int>(length),static_cast<int>(stream.offset))==SQLITE_OK;
    const bool closed=sqlite3_blob_close(blob)==SQLITE_OK;
    if (!read || !closed) return stream_detail::io();
  } else if (length) {
    DWORD received=0;
    if (!stream.file || !stream_detail::seek(stream.file.get(),stream.offset) || !ReadFile(stream.file.get(),bytes.data(),static_cast<DWORD>(length),&received,nullptr) || received!=length) return stream_detail::io();
  }
  stream.offset+=bytes.size(); const bool eof=stream.offset==stream.size;
  Json result{{"processedBytes",stream.offset},{"totalBytes",stream.size},{"eof",eof}};
  if (stream.encoding=="binary") result["data"]=TextCodec::base64(bytes);
  else {
    const auto decoded=TextCodec::decode(std::move(bytes),stream.encoding,stream.carry,stream.first,eof);
    if (const auto error=std::get_if<RuntimeError>(&decoded)) return *error;
    result["text"]=std::get<std::string>(decoded);
  }
  stream.first=false; result["encoding"]=stream.encoding; return result;
}
RuntimeResult<Json> DataStore::appendStream(Stream &stream,const Json &params) {
  if (!stream.writing || params["index"].get<uint64_t>()!=stream.index) return RuntimeError{"STREAM_SEQUENCE","写入块顺序不合法，请取消事务后重试"};
  std::string bytes;
  if (stream.encoding=="binary") {
    if (!params.contains("data")) return RuntimeError{"INVALID_REQUEST","字节流需要 data 参数"};
    const auto decoded=TextCodec::unbase64(params["data"].get<std::string>());
    if (!decoded || decoded->size()>CHUNK_BYTES) return RuntimeError{"INVALID_REQUEST","二进制块不是有效 Base64 或超过 64 KiB"};
    bytes=*decoded;
  } else {
    if (!params.contains("text")) return RuntimeError{"INVALID_REQUEST","文本流需要 text 参数"};
    const auto encoded=TextCodec::encode(params["text"].get<std::string>(),stream.encoding,stream.first);
    if (const auto error=std::get_if<RuntimeError>(&encoded)) return *error;
    bytes=std::get<std::string>(encoded);
  }
  uint64_t staged=0;
  for (const auto &[id,active]:streams_) if (active.writing) staged+=active.offset;
  if (bytes.size()>MAX_STAGED_BYTES-staged || (stream.privateFile && bytes.size()>MAX_PRIVATE_BYTES-stream.offset))
    return RuntimeError{"QUOTA_EXCEEDED","超过写入暂存预算或单个私有文件容量"};
  DWORD written=0;
  if (!WriteFile(stream.file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size()) return stream_detail::io();
  stream.first=false; ++stream.index; stream.offset+=bytes.size();
  return Json{{"index",stream.index},{"writtenBytes",stream.offset}};
}
RuntimeResult<Json> DataStore::commitPrivate(Stream &stream,const DataLease &lease) {
  if (!stream_detail::sql(db_,"BEGIN IMMEDIATE")) return stream_detail::io();
  const auto fail=[&](RuntimeError error)->RuntimeResult<Json> { stream_detail::sql(db_,"ROLLBACK"); return error; };
  const auto snapshot=privateSnapshot(stream.name);
  if (const auto error=std::get_if<RuntimeError>(&snapshot)) return fail(*error);
  const auto &current=std::get<Json>(snapshot);
  if ((current.is_null()?"missing":current.dump())!=stream.revision) return fail(stream_detail::changed());
  {
    stream_detail::Query quota(db_,"SELECT count(*),coalesce(sum(length(CAST(value AS BLOB))),0) FROM content WHERE kind='file' AND name<>?");
    if (!quota.text(1,stream.name) || quota.step()!=SQLITE_ROW) return fail(stream_detail::io());
    if (quota.number(0)>=128 || static_cast<uint64_t>(quota.number(1))+stream.offset>128ULL*1024*1024)
      return fail({"QUOTA_EXCEEDED","私有文件超过 128 个对象或 128 MiB 总容量"});
    stream_detail::Query upsert(db_,"INSERT INTO content(kind,name,value,revision) VALUES('file',?,zeroblob(?),?) ON CONFLICT(kind,name) DO UPDATE SET value=excluded.value,revision=excluded.revision");
    const auto revision=FileAccess::randomId();
    if (revision.empty() || !upsert.text(1,stream.name) || !upsert.number(2,static_cast<int64_t>(stream.offset)) || !upsert.text(3,revision) || upsert.step()!=SQLITE_DONE) return fail(stream_detail::io());
  }
  const auto inserted=privateSnapshot(stream.name);
  if (const auto error=std::get_if<RuntimeError>(&inserted)) return fail(*error);
  sqlite3_blob *blob=nullptr;
  if (sqlite3_blob_open(db_,"main","content","value",std::get<Json>(inserted)["rowid"].get<int64_t>(),1,&blob)!=SQLITE_OK) return fail(stream_detail::io());
  bool valid=stream_detail::seek(stream.file.get(),0);
  std::string buffer(CHUNK_BYTES,'\0');
  for (uint64_t offset=0;valid && offset<stream.offset;offset+=CHUNK_BYTES) {
    const auto size=static_cast<DWORD>(std::min<uint64_t>(CHUNK_BYTES,stream.offset-offset)); DWORD received=0;
    valid=lease.valid() && ReadFile(stream.file.get(),buffer.data(),size,&received,nullptr) && received==size &&
      sqlite3_blob_write(blob,buffer.data(),static_cast<int>(size),static_cast<int>(offset))==SQLITE_OK;
  }
  if (sqlite3_blob_close(blob)!=SQLITE_OK) valid=false;
  if (!lease.valid()) return fail({"REQUEST_EXPIRED","私有文件提交已取消，原内容已保留"});
  if (!valid || !stream_detail::sql(db_,"COMMIT")) return fail(stream_detail::io());
  return Json(nullptr);
}
RuntimeResult<Json> DataStore::commitStream(Stream &stream,const DataLease &lease) {
  if (!stream.writing) return RuntimeError{"INVALID_REQUEST","读取流不能提交写入"};
  // 空文本也要写入所选编码的 BOM；字节流空文件保持零字节。
  if (stream.first && stream.encoding!="binary") {
    const auto appended=appendStream(stream,Json{{"index",0},{"text",""}});
    if (const auto error=std::get_if<RuntimeError>(&appended)) return *error;
  }
  if (stream.privateFile) return commitPrivate(stream,lease);
  const auto found=handles_.find(stream.handle);
  if (found==handles_.end()) return RuntimeError{"HANDLE_EXPIRED","保存目标句柄已经关闭"};
  auto &grant=found->second;
  if (const auto error=unchanged(grant)) return *error;
  if (!FlushFileBuffers(stream.file.get())) return stream_detail::io();
  if (!lease.valid()) return RuntimeError{"REQUEST_EXPIRED","文件提交已取消，原内容已保留"};
  stream.file.reset();
  auto backup=stream.temporary; backup.replace_extension(".bak");
  const bool success=grant.file?ReplaceFileW(grant.path.c_str(),stream.temporary.c_str(),backup.c_str(),0,nullptr,nullptr)!=0:
    MoveFileExW(stream.temporary.c_str(),grant.path.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
  if (!success) {
    const auto code=GetLastError();
    if (code==ERROR_UNABLE_TO_MOVE_REPLACEMENT || code==ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
      stream.temporary.clear(); grant.writable=false;
      return RuntimeError{"FILE_RECOVERY_REQUIRED","文件替换中断，保存目录内的 .alkaid-transfer-*.bak/.tmp 已保留，请检查恢复文件"};
    }
    return stream_detail::io();
  }
  stream.temporary.clear(); DeleteFileW(backup.c_str());
  grant.file=FileAccess::open(grant.path); grant.contentHash.clear();
  if (!grant.file || !FileAccess::regular(grant.file.get()) || !GetFileInformationByHandle(grant.file.get(),&grant.snapshot)) {
    grant.writable=false; return RuntimeError{"FILE_REOPEN_FAILED","内容已保存，但无法继续持有文件，请重新打开"};
  }
  return Json(nullptr);
}
RuntimeResult<Json> DataStore::streamCall(const std::string &method,const Json &params,uint64_t session,const DataLease &lease) {
  expireStreams();
  if (method.starts_with("files.begin")) return beginStream(method,params,session,lease);
  const auto id=params["streamId"].get<std::string>();
  const auto found=streams_.find(id);
  if (found==streams_.end() || found->second.session!=session) return RuntimeError{"STREAM_EXPIRED","文件流不存在、不属于当前页面或已过期"};
  auto &stream=found->second; stream.touched=RuntimeClock::now();
  if (method=="files.abortStream") { releaseStream(id); return Json(nullptr); }
  RuntimeResult<Json> result=method=="files.readNext"?readStream(stream):method=="files.appendChunk"?appendStream(stream,params):commitStream(stream,lease);
  if (method=="files.commitWrite" || std::holds_alternative<RuntimeError>(result) ||
      (method=="files.readNext" && std::get<Json>(result)["eof"]==true)) releaseStream(id);
  return result;
}
}
