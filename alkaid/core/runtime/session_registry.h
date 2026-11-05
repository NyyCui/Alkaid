/** @file session_registry.h 应用注册、浏览器绑定与文档会话管理。 */
#ifndef ALKAID_SESSION_REGISTRY_H
#define ALKAID_SESSION_REGISTRY_H

#include "runtime_types.h"
#include <map>

namespace alkaid {

/** 所有方法必须在宿主同一调度线程调用；工作线程仅持有不可变副本。 */
class SessionRegistry {
public:
  /** 注册已通过安装校验的应用，不允许覆盖应用或复用来源。 */
  std::optional<RuntimeError> registerApp(AppIdentity identity, AppAuthorization authorization);
  /** 将实际 browserId 绑定到宿主选定的应用。 */
  std::optional<RuntimeError> bindBrowser(int64_t browserId, const std::string &appId);
  /** 导航开始时先撤销旧会话；只有受信主文档可以建立新会话。 */
  RuntimeResult<uint64_t> openDocument(const DocumentIdentity &document);
  /** 页面导航开始即调用，不能等新页面创建后才撤销。 */
  void invalidateDocument(int64_t browserId);
  /** 销毁浏览器并撤销其全部身份关联。 */
  void unbindBrowser(int64_t browserId);
  /** 关闭应用并撤销所有会话，重新安装不能继承旧调用。 */
  void unregisterApp(const std::string &appId);
  /** 更新宿主授权，已经发出的调用在完成时也会重新检查。 */
  std::optional<RuntimeError> setAuthorization(const std::string &appId, AppAuthorization authorization);
  /** 以真实文档和会话代次验证来源，不使用前端自报 appId。 */
  std::optional<SessionRecord> resolve(uint64_t sessionId, const DocumentIdentity &document) const;
  std::optional<SessionRecord> find(uint64_t sessionId) const;
  bool allows(uint64_t sessionId, const std::string &capability, bool managerOnly) const;

private:
  struct AppRecord {
    AppIdentity identity;
    AppAuthorization authorization;
  };
  std::map<std::string, AppRecord> apps_;
  std::map<int64_t, std::string> browsers_;
  std::map<uint64_t, SessionRecord> sessions_;
  uint64_t nextSessionId_ = 1;
};

} // namespace alkaid
#endif // ALKAID_SESSION_REGISTRY_H
