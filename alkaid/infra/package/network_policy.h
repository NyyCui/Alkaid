/** @file network_policy.h 签名清单中的网络端点、规范路径和独立授权指纹。 */
#ifndef ALKAID_NETWORK_POLICY_H
#define ALKAID_NETWORK_POLICY_H
#include "package_policy.h"
#include "crypto.h"
namespace alkaid::NetworkPolicy {
inline bool identifier(const std::string &s) {
  return !s.empty() && s.size()<=40 && std::ranges::all_of(s,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
}
inline bool path(const std::string &s,bool query=true) {
  if (s.empty() || s.size()>4096 || s.front()!='/' || s.starts_with("//")) return false;
  const auto split=s.find('?'); const auto pathname=s.substr(0,split);
  if (!query && split!=std::string::npos) return false;
  // 路径不接受百分号，避免代理/服务器多次解码导致越过授权目录；查询可携带规范编码。
  if (pathname.find('%')!=std::string::npos || pathname.find("//")!=std::string::npos) return false;
  for(const char c:pathname)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_'||c=='.'||c=='~'))return false;
  for (std::size_t at=1;at<=pathname.size();) {
    const auto end=pathname.find('/',at); const auto part=pathname.substr(at,end==std::string::npos?end:end-at);
    if (part=="." || part=="..") return false;
    if(end==std::string::npos) break; at=end+1;
  }
  for (const unsigned char c:s) if(c<=32||c>=127||c=='\\'||c=='#') return false;
  for(std::size_t i=0;i<s.size();++i) if(s[i]=='%') {
    if(i+2>=s.size() || PackageCrypto::unhex(PackagePolicy::lower(s.substr(i+1,2))).size()!=1) return false;
    const auto b=PackageCrypto::unhex(PackagePolicy::lower(s.substr(i+1,2)))[0]; if(b<32||b==127) return false; i+=2;
  }
  return true;
}
struct Url { std::string host,path; unsigned short port=443; bool secure=true; };
inline std::optional<Url> url(const std::string &value) {
  Url result; std::size_t start=8;
  if(value.starts_with("http://")){result.secure=false;result.port=80;start=7;} else if(!value.starts_with("https://")) return std::nullopt;
  const auto slash=value.find('/',start); if(slash==std::string::npos) return std::nullopt;
  auto authority=value.substr(start,slash-start); result.path=value.substr(slash);
  const auto colon=authority.find(':');
  if(colon!=std::string::npos){
    const auto port=authority.substr(colon+1); unsigned int number=0;
    const auto parsed=std::from_chars(port.data(),port.data()+port.size(),number);
    if(port.empty()||parsed.ec!=std::errc{}||parsed.ptr!=port.data()+port.size()||!number||number>65535||std::to_string(number)!=port) return std::nullopt;
    result.port=static_cast<unsigned short>(number); authority.resize(colon);
  }
  if(authority.empty()||authority.size()>253||authority.back()=='.'||authority.find('.')==std::string::npos) return std::nullopt;
  for(const char c:authority) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.')) return std::nullopt;
  std::size_t offset=0;
  while(offset<authority.size()) { const auto end=authority.find('.',offset); const auto part=authority.substr(offset,end==std::string::npos?end:end-offset);
    if(part.empty()||part.size()>63||part.front()=='-'||part.back()=='-') return std::nullopt;
    if(end==std::string::npos) break; offset=end+1;
  }
  if(!path(result.path,false)||result.path.back()!='/') return std::nullopt;
  result.host=authority; return result;
}
inline bool endpoints(const Json &items) {
  if(!items.is_array()||items.empty()||items.size()>8) return false;
  std::set<std::string> names;
  for(const auto &item:items) {
    if(!PackagePolicy::keys(item,{"id","baseUrl","methods","allowPrivateNetwork"}) || !item["id"].is_string() || !item["baseUrl"].is_string() ||
       !item["allowPrivateNetwork"].is_boolean() || !item["methods"].is_array() || item["methods"].empty() || item["methods"].size()>6) return false;
    const auto id=item["id"].get<std::string>(); const auto parsed=url(item["baseUrl"].get<std::string>());
    if(!identifier(id)||!names.insert(id).second||!parsed) return false;
    if(!parsed->secure && (parsed->host!="127.0.0.1" || !item["allowPrivateNetwork"].get<bool>())) return false;
    std::set<std::string> methods;
    for(const auto &method:item["methods"]) {
      if(!method.is_string()) return false; const auto m=method.get<std::string>();
      if((m!="GET"&&m!="HEAD"&&m!="POST"&&m!="PUT"&&m!="PATCH"&&m!="DELETE")||!methods.insert(m).second) return false;
    }
  }
  return true;
}
inline std::string scope(Json endpoint) {
  auto methods=endpoint["methods"].get<std::set<std::string>>(); endpoint["methods"]=methods;
  return "net.endpoint:"+PackageCrypto::sha256(endpoint.dump());
}
}
#endif // ALKAID_NETWORK_POLICY_H
