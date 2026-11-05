/** @file runtime_types.h 宿主内部身份、权限与错误类型，不作为 Renderer 的身份凭据。 */
#ifndef ALKAID_RUNTIME_TYPES_H
#define ALKAID_RUNTIME_TYPES_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <variant>

namespace alkaid {

struct RuntimeError {
  std::string code;
  std::string message;
};

template <typename T>
using RuntimeResult = std::variant<T, RuntimeError>;

struct AppIdentity {
  std::string appId;
  std::string originKey;
  std::set<std::string> declaredCapabilities;
};

/** 权限集合由宿主策略和用户授权分别维护，应用不能自行填入。 */
struct AppAuthorization {
  std::set<std::string> policyCapabilities;
  std::set<std::string> grantedCapabilities;
  bool platformManager = false;
};

/** 由 CEF 适配层从真实回调取得，不能从请求 JSON 反序列化。 */
struct DocumentIdentity {
  int64_t browserId = 0;
  std::string frameId;
  bool mainFrame = false;
  std::string origin;
};

struct SessionRecord {
  uint64_t sessionId = 0;
  std::string appId;
  DocumentIdentity document;
};

struct CallPermit {
  uint64_t sessionId = 0;
  std::string requestId;
  // 宿主内部代次防止请求 ID 被复用后，旧结果错误完成新请求。
  uint64_t ticket = 0;
};

using RuntimeClock = std::chrono::steady_clock;

} // namespace alkaid
#endif // ALKAID_RUNTIME_TYPES_H
