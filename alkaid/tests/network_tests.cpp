/** @file network_tests.cpp 端点授权、地址分类、路径边界与真实 WinHTTP 测试入口。 */
#include "network_store.h"
#include "network_policy.h"
#include "text_codec.h"
#include <iostream>
namespace {int count=0;bool check(bool value,int line){++count;if(!value)std::cerr<<"断言失败："<<line<<'\n';return value;}}
#define CHECK(x) if(!check((x),__LINE__))return 1
int main(int argc,char **argv) {
  using alkaid::Json;using alkaid::NetworkStore;namespace Policy=alkaid::NetworkPolicy;
  for(const auto *address:{"127.0.0.1","10.1.2.3","172.16.0.1","192.168.1.1","100.64.0.1","::1","fc00::1","::ffff:127.0.0.1"}) {CHECK(!NetworkStore::addressAllowed(address,false));CHECK(NetworkStore::addressAllowed(address,true));}
  for(const auto *address:{"0.0.0.0","169.254.169.254","224.0.0.1","255.255.255.255","192.0.2.1","198.18.0.1","203.0.113.1","fe80::1","2001:db8::1","2002:7f00:1::","::","not-an-ip"}) {CHECK(!NetworkStore::addressAllowed(address,false));CHECK(!NetworkStore::addressAllowed(address,true));}
  for(const auto *address:{"1.1.1.1","8.8.8.8","2606:4700:4700::1111"})CHECK(NetworkStore::addressAllowed(address,false));
  Json endpoint={{"id","test"},{"baseUrl","https://example.com/api/"},{"methods",{"GET","POST"}},{"allowPrivateNetwork",false}};
  CHECK(Policy::endpoints(Json::array({endpoint})));const auto scope=Policy::scope(endpoint);endpoint["methods"]={"POST","GET"};CHECK(scope==Policy::scope(endpoint));endpoint["baseUrl"]="https://example.com/";CHECK(scope!=Policy::scope(endpoint));
  for(const auto *path:{"//evil.test/","/../outside","/%2e%2e/","/foo\\bar","/a#fragment","/a?x=%0d","/a?x=%zz"})CHECK(!Policy::path(path));
  CHECK(Policy::path("/search?q=%E4%B8%AD%E6%96%87&n=2"));
  endpoint["baseUrl"]="http://127.0.0.1:8080/";CHECK(!Policy::endpoints(Json::array({endpoint})));endpoint["allowPrivateNetwork"]=true;CHECK(Policy::endpoints(Json::array({endpoint})));
  endpoint["baseUrl"]="http://example.com/";CHECK(!Policy::endpoints(Json::array({endpoint})));
  if(argc>=2) {
    // 由测试脚本启动的本机 HTTP 服务，只接受显式授权的测试端点。
    const bool publicProbe=std::string(argv[1])=="--public";
    endpoint["baseUrl"]=publicProbe?"https://httpbin.org/":std::string("http://127.0.0.1:")+argv[1]+"/";endpoint["methods"]={"GET","POST"};endpoint["allowPrivateNetwork"]=!publicProbe;
    NetworkStore store(Json::array({endpoint}));alkaid::DataLease lease;lease.deadline=alkaid::RuntimeClock::now()+std::chrono::seconds(15);
    const auto params=[&](const std::string &path,uint64_t limit=2*1024*1024){return Json{{"endpoint","test"},{"path",path},{"method","GET"},{"headers",Json::object()},{"bodyBase64",""},{"maxResponseBytes",limit}};};
    const auto response=store.call("net.open",params(publicProbe?"/get":"/json"),1,lease);
    if(const auto error=std::get_if<alkaid::RuntimeError>(&response)){std::cerr<<error->code<<":"<<error->message<<'\n';return 1;}
    const auto meta=std::get<Json>(response);CHECK(meta["status"]==200);std::string data;
    while(true){const auto result=store.call("net.readNext",{{"transferId",meta["transferId"]}},1,lease);CHECK(std::holds_alternative<Json>(result));const auto &chunk=std::get<Json>(result);data+=*alkaid::TextCodec::unbase64(chunk["data"]);if(chunk["eof"]==true)break;}
    CHECK(publicProbe?Json::parse(data).contains("url"):Json::parse(data)["message"]=="来自真实网络服务");
    CHECK(!meta["headers"].contains("set-cookie"));
    if(!publicProbe) {
      auto invalid=params("/json");invalid["headers"]={{"host","evil.test"}};CHECK(!NetworkStore::validParams("net.open",invalid));
      invalid=params("/json");invalid["method"]="DELETE";CHECK(std::get<alkaid::RuntimeError>(store.call("net.open",invalid,1,lease)).code=="PERMISSION_DENIED");
      const auto binary=std::get<Json>(store.call("net.open",params("/binary"),1,lease));CHECK(binary["totalBytes"]==1048613);
      CHECK(std::get<alkaid::RuntimeError>(store.call("net.readNext",{{"transferId",binary["transferId"]}},2,lease)).code=="STREAM_EXPIRED");
      std::size_t offset=0;while(true){const auto chunk=std::get<Json>(store.call("net.readNext",{{"transferId",binary["transferId"]}},1,lease));const auto raw=*alkaid::TextCodec::unbase64(chunk["data"]);for(const unsigned char c:raw)CHECK(c==(offset++%256));if(chunk["eof"]==true)break;}CHECK(offset==1048613);
      CHECK(std::get<alkaid::RuntimeError>(store.call("net.open",params("/binary",100),1,lease)).code=="RESPONSE_TOO_LARGE");
      const auto limited=std::get<Json>(store.call("net.open",params("/chunked",100),1,lease));
      CHECK(std::get<alkaid::RuntimeError>(store.call("net.readNext",{{"transferId",limited["transferId"]}},1,lease)).code=="RESPONSE_TOO_LARGE");
      const auto redirect=std::get<Json>(store.call("net.open",params("/redirect"),1,lease));CHECK(redirect["status"]==302);CHECK(redirect["headers"]["location"]=="/forbidden-target");store.release(redirect["transferId"]);
      std::vector<std::string> ids;for(int i=0;i<4;i++)ids.push_back(std::get<Json>(store.call("net.open",params("/json"),1,lease))["transferId"]);
      CHECK(std::get<alkaid::RuntimeError>(store.call("net.open",params("/json"),1,lease)).code=="RESOURCE_EXHAUSTED");store.revoke(1);
      for(const auto &id:ids)CHECK(std::get<alkaid::RuntimeError>(store.call("net.readNext",{{"transferId",id}},1,lease)).code=="STREAM_EXPIRED");
      if(argc>=3) {
        endpoint["baseUrl"]=std::string("https://127.0.0.1:")+argv[2]+"/";endpoint["allowPrivateNetwork"]=false;NetworkStore denied(Json::array({endpoint}));
        const auto failure=denied.call("net.open",params("/json"),1,lease);CHECK(std::holds_alternative<alkaid::RuntimeError>(failure));CHECK(std::get<alkaid::RuntimeError>(failure).code=="NETWORK_ADDRESS_DENIED");
        endpoint["allowPrivateNetwork"]=true;NetworkStore invalidTls(Json::array({endpoint}));
        const auto tls=invalidTls.call("net.open",params("/json"),1,lease);CHECK(std::holds_alternative<alkaid::RuntimeError>(tls));CHECK(std::get<alkaid::RuntimeError>(tls).code=="TLS_ERROR");
      }
    }
  }
  std::cout<<"网络策略验证通过："<<count<<" 条断言\n";return 0;
}
