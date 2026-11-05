/** @file network_store.cpp WinHTTP 执行网络访问；连接地址在发送 HTTP 前校验，不共享凭据或 Cookie。 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include "network_store.h"
#include "network_policy.h"
#include "text_codec.h"
namespace alkaid {
namespace network_detail {
std::wstring wide(const std::string &text) { const auto value=TextCodec::toWide(text,CP_UTF8); const auto out=std::get_if<std::wstring>(&value); return out?*out:L""; }
std::string utf8(const std::wstring &text) { const auto value=TextCodec::wideTo(text,CP_UTF8); const auto out=std::get_if<std::string>(&value); return out?*out:""; }
RuntimeError failure(DWORD code) {
  if(code==ERROR_WINHTTP_TIMEOUT) return {"TIMEOUT","网络请求超时，服务端可能已经处理，请勿盲目重试写入"};
  if(code==ERROR_WINHTTP_SECURE_FAILURE) return {"TLS_ERROR","HTTPS 证书或安全连接验证失败"};
  if(code==ERROR_WINHTTP_NAME_NOT_RESOLVED) return {"NETWORK_DNS","无法解析授权端点的主机名"};
  return {"NETWORK_ERROR","网络请求失败，请检查网络连接与端点服务"};
}
RuntimeError expired() { return {"REQUEST_EXPIRED","网络请求已取消、超时或页面已关闭"}; }
bool token(const Json &p) { return p.contains("transferId")&&p["transferId"].is_string()&&p["transferId"].get_ref<const std::string&>().size()==64&&PackageCrypto::unhex(p["transferId"].get<std::string>()).size()==32; }
bool address(const sockaddr_storage &a,bool allowPrivate) {
  if(a.ss_family==AF_INET) {
    const auto ip=ntohl(reinterpret_cast<const sockaddr_in*>(&a)->sin_addr.s_addr);
    const auto first=ip>>24, second=(ip>>16)&255;
    if(first==0||first>=224||(first==169&&second==254)||(ip>>8)==0xc00000||(ip>>8)==0xc00002||
       (ip>>8)==0xc05863||(ip>>15)==0x18c24||(ip>>8)==0xc63364||(ip>>8)==0xcb0071) return false;
    const bool local=first==10||first==127||(first==172&&second>=16&&second<=31)||(first==192&&second==168)||(first==100&&second>=64&&second<=127);
    return !local||allowPrivate;
  }
  if(a.ss_family==AF_INET6) {
    const auto &ip=reinterpret_cast<const sockaddr_in6*>(&a)->sin6_addr;
    if(IN6_IS_ADDR_V4MAPPED(&ip)) { sockaddr_storage v4{}; auto *out=reinterpret_cast<sockaddr_in*>(&v4);out->sin_family=AF_INET;std::memcpy(&out->sin_addr,ip.s6_addr+12,4);return address(v4,allowPrivate); }
    if(IN6_IS_ADDR_LOOPBACK(&ip)||(ip.s6_addr[0]&0xfe)==0xfc) return allowPrivate;
    if((ip.s6_addr[0]&0xe0)!=0x20) return false;
    if(ip.s6_addr[0]==0x20&&ip.s6_addr[1]==0x02) return false;
    if(ip.s6_addr[0]==0x20&&ip.s6_addr[1]==0x01&&((ip.s6_addr[2]==0x0d&&ip.s6_addr[3]==0xb8)||ip.s6_addr[2]<2)) return false;
    if(ip.s6_addr[0]==0x3f&&ip.s6_addr[1]==0xff&&(ip.s6_addr[2]&0xf0)==0) return false;
    return true;
  }
  return false;
}
}
bool NetworkStore::addressAllowed(const std::string &value,bool allowPrivate) {
  sockaddr_storage address{}; auto *v4=reinterpret_cast<sockaddr_in*>(&address); v4->sin_family=AF_INET;
  if(InetPtonA(AF_INET,value.c_str(),&v4->sin_addr)==1) return network_detail::address(address,allowPrivate);
  auto *v6=reinterpret_cast<sockaddr_in6*>(&address); v6->sin6_family=AF_INET6;
  return InetPtonA(AF_INET6,value.c_str(),&v6->sin6_addr)==1&&network_detail::address(address,allowPrivate);
}
struct NetworkStore::Stream {
  HINTERNET internet=nullptr,connection=nullptr,request=nullptr;
  HANDLE ended=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::atomic_bool closed{false},verified{false},blocked{false}; bool callbackSet=false,privateNetwork=false;
  uint64_t session=0,bytes=0,limit=0; RuntimeClock::time_point touched=RuntimeClock::now();
  ~Stream() {
    if(request && !closed.exchange(true)) WinHttpCloseHandle(request);
    // 最终 HANDLE_CLOSING 之后才销毁回调上下文，避免取消时的悬空访问。
    if(callbackSet) WaitForSingleObject(ended,INFINITE);
    if(connection) WinHttpCloseHandle(connection); if(internet) WinHttpCloseHandle(internet); if(ended) CloseHandle(ended);
  }
  static void CALLBACK status(HINTERNET handle,DWORD_PTR context,DWORD status,void *info,DWORD length) {
    auto *self=reinterpret_cast<Stream*>(context); if(!self) return;
    if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(self->ended); return; }
    bool denied=false;
    if(status==WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER||status==WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER) {
      if(!info||!length) denied=true;
      else {
        // WinHTTP 的字符串通知不能统一按字节数解释长度；地址是以 NUL 结尾的 LPWSTR。
        const auto *text=static_cast<const wchar_t*>(info);
        std::wstring address(text,wcsnlen_s(text,std::min<DWORD>(length,64)));
        denied=!NetworkStore::addressAllowed(network_detail::utf8(address),self->privateNetwork);
      }
      self->verified.store(!denied);
    } else if(status==WINHTTP_CALLBACK_STATUS_SENDING_REQUEST) denied=!self->verified.load();
    if(denied) { self->blocked.store(true); if(!self->closed.exchange(true)) WinHttpCloseHandle(handle); }
  }
};
NetworkStore::NetworkStore(Json endpoints):endpoints_(std::move(endpoints)) {}
NetworkStore::~NetworkStore()=default;
bool NetworkStore::validParams(const std::string &method,const Json &p) {
  if(method=="net.getEndpoints") return PackagePolicy::keys(p,{});
  if(method=="net.readNext"||method=="net.close") return PackagePolicy::keys(p,{"transferId"})&&network_detail::token(p);
  if(method!="net.open"||!PackagePolicy::keys(p,{"endpoint","path","method","headers","bodyBase64","maxResponseBytes"})||
     !p["endpoint"].is_string()||!NetworkPolicy::identifier(p["endpoint"].get<std::string>())||!p["path"].is_string()||!NetworkPolicy::path(p["path"].get<std::string>())||
     !p["method"].is_string()||!p["headers"].is_object()||p["headers"].size()>16||p["headers"].dump().size()>16384||
     !p["bodyBase64"].is_string()||p["bodyBase64"].get_ref<const std::string&>().size()>349528||
     !p["maxResponseBytes"].is_number_integer()||p["maxResponseBytes"].get<int64_t>()<0||p["maxResponseBytes"].get<int64_t>()>256LL*1024*1024) return false;
  const std::set<std::string> allowed{"accept","content-type","authorization","if-match","if-none-match","if-modified-since","idempotency-key","x-api-key","x-request-id","x-correlation-id"};
  for(const auto &[key,value]:p["headers"].items()) {
    if(!allowed.contains(key)||!value.is_string()) return false;
    const auto &v=value.get_ref<const std::string&>();for(const unsigned char ch:v)if(ch<32||ch>=127)return false;
  }
  return true;
}
void NetworkStore::release(const std::string &id) { streams_.erase(id); }
void NetworkStore::revoke(uint64_t session) { for(auto it=streams_.begin();it!=streams_.end();) {if(it->second->session==session)it=streams_.erase(it);else ++it;} }
void NetworkStore::expire() { for(auto it=streams_.begin();it!=streams_.end();) {if(RuntimeClock::now()-it->second->touched>std::chrono::seconds(60))it=streams_.erase(it);else ++it;} }
RuntimeResult<Json> NetworkStore::open(const Json &p,uint64_t session,const DataLease &lease) {
  if(streams_.size()>=4) return RuntimeError{"RESOURCE_EXHAUSTED","每个应用最多保留 4 个网络响应流"};
  const Json *endpoint=nullptr; for(const auto &item:endpoints_)if(item["id"]==p["endpoint"])endpoint=&item;
  if(!endpoint||std::ranges::find((*endpoint)["methods"],p["method"])==(*endpoint)["methods"].end()) return RuntimeError{"PERMISSION_DENIED","端点或请求方法尚未获得授权"};
  const auto url=NetworkPolicy::url((*endpoint)["baseUrl"].get<std::string>()); if(!url)return RuntimeError{"PERMISSION_DENIED","端点配置不合法"};
  const auto body=TextCodec::unbase64(p["bodyBase64"].get<std::string>());
  if(!body||body->size()>256*1024||((p["method"]=="GET"||p["method"]=="HEAD")&&!body->empty())) return RuntimeError{"INVALID_REQUEST","请求正文不合法、超过 256 KiB，或 GET/HEAD 携带正文"};
  auto stream=std::make_unique<Stream>();stream->session=session;stream->limit=p["maxResponseBytes"].get<uint64_t>();stream->privateNetwork=(*endpoint)["allowPrivateNetwork"].get<bool>();
  if(!stream->ended) return network_detail::failure(GetLastError());
  stream->internet=WinHttpOpen(L"Alkaid/2",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
  if(!stream->internet) return network_detail::failure(GetLastError());
  // 不使用系统代理或跨会话连接池，避免实际目的地址被隐藏和不同应用共享连接。
  DWORD pooling=1; if(!WinHttpSetOption(stream->internet,WINHTTP_OPTION_DISABLE_GLOBAL_POOLING,&pooling,sizeof(pooling))) return network_detail::failure(GetLastError());
  const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(lease.deadline-RuntimeClock::now()).count();
  if(!lease.valid()) return network_detail::expired();
  const auto timeout=static_cast<int>(std::clamp<int64_t>(remaining,1,15000));
  WinHttpSetTimeouts(stream->internet,timeout,timeout,timeout,timeout);
  stream->connection=WinHttpConnect(stream->internet,network_detail::wide(url->host).c_str(),url->port,0);
  if(!stream->connection) return network_detail::failure(GetLastError());
  const auto path=url->path+p["path"].get<std::string>().substr(1);
  stream->request=WinHttpOpenRequest(stream->connection,network_detail::wide(p["method"].get<std::string>()).c_str(),network_detail::wide(path).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,url->secure?WINHTTP_FLAG_SECURE:0);
  if(!stream->request) return network_detail::failure(GetLastError());
  DWORD features=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_KEEP_ALIVE;
  DWORD auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,headerLimit=65536,contextSize=sizeof(DWORD_PTR);DWORD_PTR context=reinterpret_cast<DWORD_PTR>(stream.get());
  if(!WinHttpSetOption(stream->request,WINHTTP_OPTION_DISABLE_FEATURE,&features,sizeof(features))||
     !WinHttpSetOption(stream->request,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof(auth))||
     !WinHttpSetOption(stream->request,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headerLimit,sizeof(headerLimit))||
     !WinHttpSetOption(stream->request,WINHTTP_OPTION_CONTEXT_VALUE,&context,contextSize)) return network_detail::failure(GetLastError());
  if(WinHttpSetStatusCallback(stream->request,Stream::status,WINHTTP_CALLBACK_FLAG_CONNECT_TO_SERVER|WINHTTP_CALLBACK_FLAG_SEND_REQUEST|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK) return network_detail::failure(GetLastError());
  stream->callbackSet=true;
  std::wstring headers=L"Accept-Encoding: identity\r\n";
  for(const auto &[key,value]:p["headers"].items()) headers+=network_detail::wide(key+": "+value.get<std::string>()+"\r\n");
  if(!lease.valid()) return network_detail::expired();
  const auto failed=[&]()->RuntimeError {if(stream->blocked.load())return {"NETWORK_ADDRESS_DENIED","实际连接地址不在端点授权范围，HTTP 请求已阻止"};if(!lease.valid())return network_detail::expired();return network_detail::failure(GetLastError());};
  if(!WinHttpSendRequest(stream->request,headers.c_str(),static_cast<DWORD>(headers.size()),body->empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body->data()),static_cast<DWORD>(body->size()),static_cast<DWORD>(body->size()),context)) return failed();
  if(stream->closed.load()||!lease.valid()) return failed();
  if(!WinHttpReceiveResponse(stream->request,nullptr)) return failed();
  if(stream->closed.load()||!lease.valid()) return failed();
  WINHTTP_CONNECTION_INFO peer{}; DWORD peerSize=sizeof(peer);
  if(!WinHttpQueryOption(stream->request,WINHTTP_OPTION_CONNECTION_INFO,&peer,&peerSize)||!network_detail::address(peer.RemoteAddress,stream->privateNetwork)) return RuntimeError{"NETWORK_ADDRESS_DENIED","无法确认实际连接地址的授权"};
  DWORD status=0,size=sizeof(status);
  if(!WinHttpQueryHeaders(stream->request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr))return failed();
  size=0;WinHttpQueryHeaders(stream->request,WINHTTP_QUERY_RAW_HEADERS_CRLF,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX);
  if(size>65536||size<sizeof(wchar_t)) return RuntimeError{"RESPONSE_TOO_LARGE","响应头超过限制"};
  std::wstring raw(size/sizeof(wchar_t),0);
  if(!WinHttpQueryHeaders(stream->request,WINHTTP_QUERY_RAW_HEADERS_CRLF,WINHTTP_HEADER_NAME_BY_INDEX,raw.data(),&size,WINHTTP_NO_HEADER_INDEX))return failed();
  Json responseHeaders=Json::object(); std::size_t at=raw.find(L"\r\n");
  while(at!=std::wstring::npos) {
    const auto next=raw.find(L"\r\n",at+2);if(next==std::wstring::npos||next==at+2)break;
    const auto line=raw.substr(at+2,next-at-2);const auto colon=line.find(':');
    if(colon!=std::wstring::npos) {const auto key=PackagePolicy::lower(network_detail::utf8(line.substr(0,colon)));auto value=line.substr(colon+1);while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(0,1);
      if(key!="set-cookie"&&key!="set-cookie2"&&key!="proxy-authenticate")responseHeaders[key]=network_detail::utf8(value);}
    at=next;
  }
  Json total=nullptr;
  if(p["method"]=="HEAD")total=0;
  else if(responseHeaders.contains("content-length")) {
    const auto text=responseHeaders["content-length"].get<std::string>();uint64_t value=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return RuntimeError{"NETWORK_ERROR","响应长度不合法"};
    if(value>stream->limit)return RuntimeError{"RESPONSE_TOO_LARGE","响应超过本次读取预算"}; total=value;
  }
  const auto id=FileAccess::randomId();if(id.empty())return network_detail::failure(0);
  streams_.emplace(id,std::move(stream));
  return Json{{"transferId",id},{"status",status},{"headers",responseHeaders},{"totalBytes",total}};
}
RuntimeResult<Json> NetworkStore::call(const std::string &method,const Json &params,uint64_t session,const DataLease &lease) {
  expire();if(!validParams(method,params))return RuntimeError{"INVALID_REQUEST","网络请求参数不合法"};
  if(!lease.valid())return network_detail::expired();
  if(method=="net.getEndpoints")return endpoints_;
  if(method=="net.open")return open(params,session,lease);
  const auto id=params["transferId"].get<std::string>();const auto found=streams_.find(id);
  if(found==streams_.end()||found->second->session!=session)return RuntimeError{"STREAM_EXPIRED","网络响应流不属于当前页面或已失效"};
  if(method=="net.close"){release(id);return Json(nullptr);}
  auto &stream=*found->second;stream.touched=RuntimeClock::now();
  const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(lease.deadline-RuntimeClock::now()).count();
  const auto timeout=static_cast<int>(std::clamp<int64_t>(remaining,1,15000)); WinHttpSetTimeouts(stream.request,timeout,timeout,timeout,timeout);
  std::string bytes(static_cast<std::size_t>(std::min<uint64_t>(65536,stream.limit-stream.bytes+1)),0);DWORD received=0;
  if(!WinHttpReadData(stream.request,bytes.data(),static_cast<DWORD>(bytes.size()),&received)){const auto error=network_detail::failure(GetLastError());release(id);return error;}
  if(!lease.valid()){release(id);return network_detail::expired();}
  if(received>stream.limit-stream.bytes){release(id);return RuntimeError{"RESPONSE_TOO_LARGE","分块响应超过本次读取预算"};}
  bytes.resize(received);stream.bytes+=received;
  Json result{{"data",TextCodec::base64(bytes)},{"processedBytes",stream.bytes},{"eof",received==0}};
  if(!received)release(id);return result;
}
}
