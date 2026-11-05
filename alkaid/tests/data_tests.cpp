/** @file data_tests.cpp 真实 SQLite 与 Windows 文件系统验证，使用独立临时目录。 */
#include "data_store.h"
#include <fstream>
#include <iostream>
namespace {
int assertions = 0;
bool check(bool value, int line) { ++assertions; if (!value) std::cerr << "断言失败，行 " << line << '\n'; return value; }
#define CHECK(condition) if (!check((condition),__LINE__)) return 1
bool error(const alkaid::RuntimeResult<alkaid::Json> &result, const char *code) {
  const auto failure = std::get_if<alkaid::RuntimeError>(&result); return failure && failure->code == code;
}
bool ok(const alkaid::RuntimeResult<alkaid::Json> &result) {
  if (const auto failure = std::get_if<alkaid::RuntimeError>(&result)) { std::cerr << failure->code << ": " << failure->message << '\n'; return false; }
  return true;
}
std::string handle(const alkaid::RuntimeResult<alkaid::Json> &result) { return std::get<alkaid::Json>(result)["handle"].get<std::string>(); }
std::string disk(const std::filesystem::path &path) { std::ifstream file(path,std::ios::binary); return {std::istreambuf_iterator<char>(file),{}}; }
void write(const std::filesystem::path &path, const std::string &text) { std::ofstream(path,std::ios::binary) << text; }
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const auto root = std::filesystem::absolute(std::filesystem::path(argv[1])) / ("run-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
  std::filesystem::create_directories(root);
  alkaid::DataLease lease; lease.deadline = alkaid::RuntimeClock::now() + std::chrono::minutes(2);
  const auto call = [&](alkaid::DataStore &store,const std::string &method,const alkaid::Json &params,uint64_t session = 1) { return store.call(method,params,session,lease); };
  {
    alkaid::DataStore first(root / "first"), second(root / "second");
    CHECK(ok(call(first,"storage.set",{{"key","theme"},{"value",{{"fontSize",23}}}})));
    CHECK(std::get<alkaid::Json>(call(first,"storage.get",{{"key","theme"}}))["value"]["fontSize"] == 23);
    CHECK(std::get<alkaid::Json>(call(second,"storage.get",{{"key","theme"}}))["found"] == false);
    CHECK(ok(call(first,"storage.set",{{"key","null-value"},{"value",nullptr}})));
    CHECK(std::get<alkaid::Json>(call(first,"storage.get",{{"key","null-value"}}))["found"] == true);
    CHECK(ok(call(first,"storage.remove",{{"key","null-value"}})));
    CHECK(ok(call(first,"storage.batch",{{"operations",alkaid::Json::array({{{"type","set"},{"key","one"},{"value",1}},{{"type","remove"},{"key","theme"}}})}})));
    CHECK(std::get<alkaid::Json>(call(first,"storage.get",{{"key","theme"}}))["found"] == false);
    CHECK(error(call(first,"storage.batch",{{"operations",alkaid::Json::array({{{"type","set"},{"key","one"},{"value",2}},{{"type","set"},{"key","bad"},{"value",std::string(65537,'x')}}})}}),"INVALID_REQUEST"));
    CHECK(std::get<alkaid::Json>(call(first,"storage.get",{{"key","one"}}))["value"] == 1);
    for (const auto name : {"../outside", "C:/secret", "file:stream", "CON", "a/b", "name."})
      CHECK(error(call(first,"files.writePrivateText",{{"name",name},{"text","内容"}}),"INVALID_REQUEST"));
    CHECK(ok(call(first,"files.writePrivateText",{{"name","draft.txt"},{"text","你好\r\nline"}})));
    CHECK(std::get<alkaid::Json>(call(first,"files.readPrivateText",{{"name","draft.txt"}})) == "你好\r\nline");
    CHECK(std::get<alkaid::Json>(call(second,"files.readPrivateText",{{"name","draft.txt"}})).is_null());
    CHECK(error(call(first,"files.writePrivateText",{{"name","too-big.txt"},{"text",std::string(alkaid::DataStore::MAX_TEXT+1,'x')}}),"INVALID_REQUEST"));
    alkaid::DataLease cancelled; cancelled.active = false; cancelled.deadline = lease.deadline;
    CHECK(error(first.call("storage.set",{{"key","cancelled"},{"value",1}},1,cancelled),"REQUEST_EXPIRED"));
    CHECK(std::get<alkaid::Json>(call(first,"storage.get",{{"key","cancelled"}}))["found"] == false);
    alkaid::DataStore quota(root / "quota");
    for (int i = 0; i < 15; ++i) CHECK(ok(call(quota,"storage.set",{{"key","key-"+std::to_string(i)},{"value",std::string(65530,'x')}})));
    CHECK(error(call(quota,"storage.batch",{{"operations",alkaid::Json::array({{{"type","set"},{"key","marker"},{"value",true}},{{"type","set"},{"key","overflow"},{"value",std::string(65530,'x')}}})}}),"QUOTA_EXCEEDED"));
    CHECK(std::get<alkaid::Json>(call(quota,"storage.get",{{"key","marker"}}))["found"] == false);
    alkaid::DataStore count(root / "count");
    for (int i = 0; i < 128; ++i) CHECK(ok(call(count,"storage.set",{{"key","item-"+std::to_string(i)},{"value",i}})));
    CHECK(error(call(count,"storage.set",{{"key","overflow"},{"value",1}}),"QUOTA_EXCEEDED"));
    const auto selected = root / "selected.txt";
    write(selected,"原始内容\r\n第二行");
    const auto readonly = first.grant(selected,false,1,lease); CHECK(ok(readonly));
    CHECK(std::get<alkaid::Json>(call(first,"files.readText",{{"handle",handle(readonly)}})) == disk(selected));
    CHECK(error(call(first,"files.writeText",{{"handle",handle(readonly)},{"text","不应写入"}}),"PERMISSION_DENIED"));
    CHECK(error(call(second,"files.readText",{{"handle",handle(readonly)}}),"HANDLE_EXPIRED"));
    CHECK(error(call(first,"files.readText",{{"handle",handle(readonly)}},2),"HANDLE_EXPIRED"));
    const auto writable = first.grant(selected,true,1,lease); CHECK(ok(writable));
    CHECK(ok(call(first,"files.writeText",{{"handle",handle(writable)},{"text","短"}})));
    CHECK(disk(selected) == "短");
    CHECK(ok(call(first,"files.writeText",{{"handle",handle(writable)},{"text",""}})));
    CHECK(disk(selected).empty());
    first.revoke(1);
    CHECK(error(call(first,"files.readText",{{"handle",handle(writable)}}),"HANDLE_EXPIRED"));
    const auto mappedPath = root / "mapped.txt"; write(mappedPath,"base");
    alkaid::FileAccess::Handle mapping;
    {
      alkaid::FileAccess::Handle file(CreateFileW(mappedPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
      CHECK(static_cast<bool>(file));
      mapping = alkaid::FileAccess::Handle(CreateFileMappingW(file.get(),nullptr,PAGE_READWRITE,0,0,nullptr));
    }
    CHECK(static_cast<bool>(mapping));
    auto view = MapViewOfFile(mapping.get(),FILE_MAP_WRITE,0,0,0); CHECK(view != nullptr);
    const auto mappedGrant = first.grant(mappedPath,true,5,lease); CHECK(error(mappedGrant,"FILE_BUSY"));
    memcpy(view,"next",4); CHECK(FlushViewOfFile(view,4) != 0); UnmapViewOfFile(view);
    mapping.reset();
    CHECK(disk(mappedPath) == "next");
    CHECK(ok(first.grant(mappedPath,true,5,lease))); first.revoke(5);
    const auto fresh = first.grant(root / "fresh.txt",true,3,lease); CHECK(ok(fresh));
    CHECK(!std::filesystem::exists(root / "fresh.txt"));
    CHECK(ok(call(first,"files.writeText",{{"handle",handle(fresh)},{"text","新建"}},3)));
    CHECK(disk(root / "fresh.txt") == "新建");
    CHECK(ok(call(first,"files.close",{{"handle",handle(fresh)}},3)));
    CHECK(error(call(first,"files.close",{{"handle",handle(fresh)}},3),"HANDLE_EXPIRED"));
    const auto conflict = first.grant(selected,true,4,lease); CHECK(ok(conflict));
    std::error_code systemError;
    std::filesystem::rename(selected, root / "renamed.txt",systemError); CHECK(!systemError); write(selected,"另一个文件");
    CHECK(error(call(first,"files.writeText",{{"handle",handle(conflict)},{"text","覆盖"}},4),"FILE_CHANGED"));
    CHECK(disk(selected) == "另一个文件"); first.revoke(4);
    write(root / "binary.txt",std::string("a\0b",3));
    { const auto selectedFile=first.grant(root / "binary.txt",false,1,lease); CHECK(ok(selectedFile)); CHECK(error(call(first,"files.readText",{{"handle",handle(selectedFile)}}),"FILE_ENCODING")); first.release(handle(selectedFile)); }
    write(root / "large.txt",std::string(alkaid::DataStore::MAX_TEXT+1,'x'));
    { const auto selectedFile=first.grant(root / "large.txt",false,1,lease); CHECK(ok(selectedFile)); CHECK(error(call(first,"files.readText",{{"handle",handle(selectedFile)}}),"FILE_TOO_LARGE")); first.release(handle(selectedFile)); }
    write(root / "invalid.txt",std::string(1,static_cast<char>(0xff)));
    { const auto selectedFile=first.grant(root / "invalid.txt",false,1,lease); CHECK(ok(selectedFile)); CHECK(error(call(first,"files.readText",{{"handle",handle(selectedFile)}}),"FILE_ENCODING")); first.release(handle(selectedFile)); }
    std::filesystem::create_hard_link(selected,root / "hard.txt",systemError); CHECK(!systemError);
    CHECK(error(first.grant(root / "hard.txt",true,1,lease),"FILE_UNSUPPORTED"));
    CHECK(error(first.grant(root / "../outside.txt",true,1,lease),"FILE_UNSUPPORTED"));
    for (int i=0;i<16;++i) CHECK(ok(first.grant(root / ("slot-"+std::to_string(i)+".txt"),true,8,lease)));
    CHECK(error(first.grant(root / "slot-overflow.txt",true,8,lease),"RESOURCE_EXHAUSTED"));
    first.revoke(8); CHECK(ok(first.grant(root / "after-revoke.txt",true,9,lease)));
  }
  {
    alkaid::DataStore reopened(root / "first");
    CHECK(std::get<alkaid::Json>(call(reopened,"storage.get",{{"key","one"}}))["value"] == 1);
    CHECK(std::get<alkaid::Json>(call(reopened,"files.readPrivateText",{{"name","draft.txt"}})) == "你好\r\nline");
    CHECK(ok(call(reopened,"files.removePrivate",{{"name","draft.txt"}})));
    CHECK(std::get<alkaid::Json>(call(reopened,"files.readPrivateText",{{"name","draft.txt"}})).is_null());
  }
  std::cout << "私有存储与文件测试通过：" << assertions << " 项断言\n";
  return 0;
}
