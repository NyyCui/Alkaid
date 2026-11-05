/** @file stream_tests.cpp 真实分块文件、编码、取消、私有事务与重启兼容验证。 */
#include "data_store.h"
#include "text_codec.h"
#include <fstream>
#include <iostream>
namespace {
int checks=0;
bool check(bool value,int line) { ++checks; if (!value) std::cerr<<"断言失败，行 "<<line<<'\n'; return value; }
#define CHECK(value) if (!check((value),__LINE__)) return 1
using Json=alkaid::Json;
bool ok(const alkaid::RuntimeResult<Json> &result) { if (const auto error=std::get_if<alkaid::RuntimeError>(&result)) { std::cerr<<error->code<<": "<<error->message<<'\n'; return false; } return true; }
bool error(const alkaid::RuntimeResult<Json> &result,const char *code) { const auto value=std::get_if<alkaid::RuntimeError>(&result); return value && value->code==code; }
std::string id(const alkaid::RuntimeResult<Json> &result,const char *key="streamId") { return std::get<Json>(result)[key].get<std::string>(); }
void put(const std::filesystem::path &path,const std::string &text) { std::ofstream(path,std::ios::binary)<<text; }
std::string get(const std::filesystem::path &path) { std::ifstream input(path,std::ios::binary); return {std::istreambuf_iterator<char>(input),{}}; }
}
int main(int argc,char **argv) {
  if (argc!=2) return 2;
  const auto root=std::filesystem::absolute(argv[1])/("run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  std::error_code ec; std::filesystem::create_directories(root,ec); CHECK(!ec);
  alkaid::DataLease lease; lease.deadline=alkaid::RuntimeClock::now()+std::chrono::minutes(5);
  std::string privateText;
  for (int i=0;i<160000;++i) privateText+="中文与emoji 🌿\r\n";
  {
    alkaid::DataStore store(root/"private");
    const auto call=[&](const std::string &method,const Json &params,uint64_t session=1) { return store.call(method,params,session,lease); };
    const auto file=root/"large.txt"; put(file,privateText);
    const auto grant=store.grant(file,false,1,lease); CHECK(ok(grant));
    const auto read=call("files.beginRead",{{"handle",id(grant,"handle")},{"encoding","auto"}}); CHECK(ok(read));
    std::string collected; std::size_t chunks=0;
    while (true) {
      const auto next=call("files.readNext",{{"streamId",id(read)}}); CHECK(ok(next)); const auto &value=std::get<Json>(next);
      CHECK(value.dump().size()<1048576); collected+=value["text"].get<std::string>(); ++chunks;
      if (value["eof"]==true) break;
    }
    CHECK(chunks>32); CHECK(collected==privateText);
    CHECK(error(call("files.readNext",{{"streamId",id(read)}}),"STREAM_EXPIRED"));
    CHECK(error(call("files.beginWrite",{{"handle",id(grant,"handle")},{"encoding","binary"}}),"PERMISSION_DENIED"));
    store.release(id(grant,"handle"));
    for (const auto *encoding:{"utf-8","utf-8-bom","utf-16le","utf-16be","gb18030"}) {
      const auto path=root/(std::string(encoding)+".txt"); put(path,"original");
      const auto selected=store.grant(path,true,1,lease); CHECK(ok(selected));
      const auto writer=call("files.beginWrite",{{"handle",id(selected,"handle")},{"encoding",encoding}}); CHECK(ok(writer));
      const std::string part=std::string(16000,'x')+"🌿中文末尾"; std::string expected;
      for (int i=0;i<9;++i) { CHECK(ok(call("files.appendChunk",{{"streamId",id(writer)},{"index",i},{"text",part}}))); expected+=part; }
      CHECK(get(path)=="original");
      CHECK(ok(call("files.commitWrite",{{"streamId",id(writer)}})));
      const auto reader=call("files.beginRead",{{"handle",id(selected,"handle")},{"encoding",std::string(encoding)=="gb18030"?"gb18030":"auto"}}); CHECK(ok(reader));
      std::string decoded;
      while (true) {
        const auto result=call("files.readNext",{{"streamId",id(reader)}}); CHECK(ok(result)); const auto &value=std::get<Json>(result);
        decoded+=value["text"].get<std::string>(); if (value["eof"]==true) break;
      }
      CHECK(decoded==expected); store.release(id(selected,"handle"));
    }
    std::string binary; for (int i=0;i<310000;++i) binary+=static_cast<char>(i%256);
    const auto binaryPath=root/"binary.dat"; put(binaryPath,binary);
    const auto binaryGrant=store.grant(binaryPath,true,1,lease); CHECK(ok(binaryGrant));
    const auto binaryRead=call("files.beginRead",{{"handle",id(binaryGrant,"handle")},{"encoding","binary"}}); CHECK(ok(binaryRead));
    std::string raw;
    while (true) { const auto next=call("files.readNext",{{"streamId",id(binaryRead)}}); CHECK(ok(next)); const auto &value=std::get<Json>(next); const auto decoded=alkaid::TextCodec::unbase64(value["data"].get<std::string>()); CHECK(decoded.has_value()); raw+=*decoded; if (value["eof"]==true) break; }
    CHECK(raw==binary);
    const auto binaryWrite=call("files.beginWrite",{{"handle",id(binaryGrant,"handle")},{"encoding","binary"}}); CHECK(ok(binaryWrite));
    for (std::size_t offset=0,index=0;offset<binary.size();offset+=65536,++index)
      CHECK(ok(call("files.appendChunk",{{"streamId",id(binaryWrite)},{"index",index},{"data",alkaid::TextCodec::base64(binary.substr(offset,65536))}})));
    CHECK(ok(call("files.commitWrite",{{"streamId",id(binaryWrite)}}))); CHECK(get(binaryPath)==binary);
    const auto invalidRead=call("files.beginRead",{{"handle",id(binaryGrant,"handle")},{"encoding","utf-8"}}); CHECK(ok(invalidRead));
    CHECK(error(call("files.readNext",{{"streamId",id(invalidRead)}}),"FILE_ENCODING"));
    const auto abortWrite=call("files.beginWrite",{{"handle",id(binaryGrant,"handle")},{"encoding","utf-8"}}); CHECK(ok(abortWrite));
    CHECK(ok(call("files.appendChunk",{{"streamId",id(abortWrite)},{"index",0},{"text","未提交"}})));
    CHECK(ok(call("files.abortStream",{{"streamId",id(abortWrite)}}))); CHECK(get(binaryPath)==binary);
    const auto sequence=call("files.beginWrite",{{"handle",id(binaryGrant,"handle")},{"encoding","binary"}}); CHECK(ok(sequence));
    CHECK(error(call("files.appendChunk",{{"streamId",id(sequence)},{"index",2},{"data","YQ=="}}),"STREAM_SEQUENCE"));
    CHECK(error(call("files.commitWrite",{{"streamId",id(sequence)}}),"STREAM_EXPIRED")); CHECK(get(binaryPath)==binary);
    const auto sessionRead=call("files.beginRead",{{"handle",id(binaryGrant,"handle")},{"encoding","binary"}}); CHECK(ok(sessionRead));
    CHECK(error(call("files.readNext",{{"streamId",id(sessionRead)}},2),"STREAM_EXPIRED"));
    store.revoke(1); CHECK(error(call("files.readNext",{{"streamId",id(sessionRead)}}),"STREAM_EXPIRED"));
    CHECK(ok(call("files.writePrivateText",{{"name","legacy.txt"},{"text","旧版私有数据"}})));
    const auto legacy=call("files.beginPrivateRead",{{"name","legacy.txt"},{"encoding","auto"}}); CHECK(ok(legacy));
    CHECK(std::get<Json>(call("files.readNext",{{"streamId",id(legacy)}}))["text"]=="旧版私有数据");
    const auto missing=call("files.beginPrivateRead",{{"name","missing.txt"},{"encoding","auto"}}); CHECK(ok(missing)); CHECK(std::get<Json>(missing).is_null());
    const auto writer=call("files.beginPrivateWrite",{{"name","large.txt"},{"encoding","binary"}}); CHECK(ok(writer));
    for (std::size_t offset=0,index=0;offset<privateText.size();offset+=65536,++index)
      CHECK(ok(call("files.appendChunk",{{"streamId",id(writer)},{"index",index},{"data",alkaid::TextCodec::base64(privateText.substr(offset,65536))}})));
    CHECK(ok(call("files.commitWrite",{{"streamId",id(writer)}})));
    CHECK(error(call("files.readPrivateText",{{"name","large.txt"}}),"FILE_TOO_LARGE"));
    const auto stale=call("files.beginPrivateRead",{{"name","legacy.txt"},{"encoding","auto"}}); CHECK(ok(stale));
    CHECK(ok(call("files.writePrivateText",{{"name","legacy.txt"},{"text","新版私有数据"}})));
    CHECK(error(call("files.readNext",{{"streamId",id(stale)}}),"FILE_CHANGED"));
    const auto conflict=call("files.beginPrivateWrite",{{"name","legacy.txt"},{"encoding","utf-8"}}); CHECK(ok(conflict));
    CHECK(ok(call("files.appendChunk",{{"streamId",id(conflict)},{"index",0},{"text","不应覆盖"}})));
    CHECK(ok(call("files.writePrivateText",{{"name","legacy.txt"},{"text","其他写入"}})));
    CHECK(error(call("files.commitWrite",{{"streamId",id(conflict)}}),"FILE_CHANGED"));
    CHECK(std::get<Json>(call("files.readPrivateText",{{"name","legacy.txt"}}))=="其他写入");
    const auto quota=call("files.beginPrivateWrite",{{"name","quota.dat"},{"encoding","binary"}}); CHECK(ok(quota));
    const auto chunk=alkaid::TextCodec::base64(std::string(65536,'x'));
    for (int i=0;i<1024;++i) CHECK(ok(call("files.appendChunk",{{"streamId",id(quota)},{"index",i},{"data",chunk}})));
    CHECK(error(call("files.appendChunk",{{"streamId",id(quota)},{"index",1024},{"data","eA=="}}),"QUOTA_EXCEEDED"));
    CHECK(std::get<Json>(call("files.readPrivateText",{{"name","quota.dat"}})).is_null());
    std::vector<std::string> readers;
    for (int i=0;i<8;++i) { const auto reader=call("files.beginPrivateRead",{{"name","large.txt"},{"encoding","binary"}}); CHECK(ok(reader)); readers.push_back(id(reader)); }
    CHECK(error(call("files.beginPrivateRead",{{"name","large.txt"},{"encoding","binary"}}),"RESOURCE_EXHAUSTED"));
    store.revoke(1);
    for (const auto &reader:readers) CHECK(error(call("files.readNext",{{"streamId",reader}}),"STREAM_EXPIRED"));
    for (const auto &entry:std::filesystem::directory_iterator(root/"private")) CHECK(!entry.path().filename().string().ends_with(".tmp"));
  }
  {
    alkaid::DataStore reopened(root/"private");
    const auto reader=reopened.call("files.beginPrivateRead",{{"name","large.txt"},{"encoding","auto"}},2,lease); CHECK(ok(reader)); std::string text;
    while (true) { const auto next=reopened.call("files.readNext",{{"streamId",id(reader)}},2,lease); CHECK(ok(next)); const auto &value=std::get<Json>(next); text+=value["text"].get<std::string>(); if (value["eof"]==true) break; }
    CHECK(text==privateText);
  }
  std::cout<<"文件流与编码验证通过，断言数量："<<checks<<'\n'; return 0;
}
