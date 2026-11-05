/** @file network_store.h 应用独立的 WinHTTP 端点请求与有界响应流，在专用线程调用。 */
#ifndef ALKAID_NETWORK_STORE_H
#define ALKAID_NETWORK_STORE_H
#include "data_store.h"
namespace alkaid {
class NetworkStore {
public:
  explicit NetworkStore(Json endpoints);
  ~NetworkStore();
  static bool validParams(const std::string &method,const Json &params);
  static bool addressAllowed(const std::string &address,bool privateNetwork);
  RuntimeResult<Json> call(const std::string &method,const Json &params,uint64_t session,const DataLease &lease);
  void release(const std::string &id);
  void revoke(uint64_t session);
  void expire();
private:
  struct Stream;
  RuntimeResult<Json> open(const Json &params,uint64_t session,const DataLease &lease);
  Json endpoints_;
  std::map<std::string,std::unique_ptr<Stream>> streams_;
};
}
#endif // ALKAID_NETWORK_STORE_H
