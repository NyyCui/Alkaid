/** @file session_registry.cpp 宿主管理的来源和授权边界。 */
#include "session_registry.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace alkaid {
namespace _ {
bool validIdentifier(const std::string &value, bool allowDot) {
  if (value.empty() || value.size() > 128) return false;
  const auto alphaNumeric = [](char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
  };
  if (!alphaNumeric(value.front()) || !alphaNumeric(value.back())) return false;
  return std::ranges::all_of(value, [&](char ch) {
    return alphaNumeric(ch) || ch == '-' || (allowDot && ch == '.');
  });
}
} // namespace _

std::optional<RuntimeError> SessionRegistry::registerApp(AppIdentity identity, AppAuthorization authorization) {
  if (!_::validIdentifier(identity.appId, true) || !_::validIdentifier(identity.originKey, false)) {
    return RuntimeError{"INVALID_APP", "应用标识或来源标识不合法"};
  }
  if (apps_.contains(identity.appId)) return RuntimeError{"APP_EXISTS", "应用已经注册"};
  for (const auto &[id, record] : apps_) {
    if (record.identity.originKey == identity.originKey) {
      return RuntimeError{"ORIGIN_EXISTS", "应用来源已经使用"};
    }
  }
  const auto appId = identity.appId;
  apps_.emplace(appId, AppRecord{std::move(identity), std::move(authorization)});
  return std::nullopt;
}

std::optional<RuntimeError> SessionRegistry::bindBrowser(int64_t browserId, const std::string &appId) {
  if (browserId <= 0 || !apps_.contains(appId)) return RuntimeError{"INVALID_APP", "浏览器或应用不存在"};
  if (browsers_.contains(browserId)) return RuntimeError{"BROWSER_EXISTS", "浏览器已经绑定"};
  browsers_.emplace(browserId, appId);
  return std::nullopt;
}

RuntimeResult<uint64_t> SessionRegistry::openDocument(const DocumentIdentity &document) {
  // 子框架既不能获取权限，也不能撤销主框架的有效会话。
  if (!document.mainFrame) return RuntimeError{"UNTRUSTED_DOCUMENT", "不允许子框架创建系统会话"};
  invalidateDocument(document.browserId);
  const auto browser = browsers_.find(document.browserId);
  if (browser == browsers_.end()) return RuntimeError{"UNTRUSTED_DOCUMENT", "浏览器未登记"};
  const auto &app = apps_.at(browser->second);
  if (document.frameId.empty() || document.origin != "alkaid://" + app.identity.originKey) {
    return RuntimeError{"UNTRUSTED_DOCUMENT", "文档来源与应用不匹配"};
  }
  if (nextSessionId_ == std::numeric_limits<uint64_t>::max()) {
    return RuntimeError{"RESOURCE_EXHAUSTED", "会话标识已耗尽"};
  }
  const auto id = nextSessionId_++;
  sessions_.emplace(id, SessionRecord{id, browser->second, document});
  return id;
}

void SessionRegistry::invalidateDocument(int64_t browserId) {
  std::erase_if(sessions_, [browserId](const auto &entry) { return entry.second.document.browserId == browserId; });
}

void SessionRegistry::unbindBrowser(int64_t browserId) {
  invalidateDocument(browserId);
  browsers_.erase(browserId);
}

void SessionRegistry::unregisterApp(const std::string &appId) {
  std::erase_if(sessions_, [&](const auto &entry) { return entry.second.appId == appId; });
  std::erase_if(browsers_, [&](const auto &entry) { return entry.second == appId; });
  apps_.erase(appId);
}

std::optional<RuntimeError> SessionRegistry::setAuthorization(const std::string &appId, AppAuthorization authorization) {
  const auto app = apps_.find(appId);
  if (app == apps_.end()) return RuntimeError{"APP_NOT_FOUND", "应用未注册"};
  app->second.authorization = std::move(authorization);
  return std::nullopt;
}

std::optional<SessionRecord> SessionRegistry::find(uint64_t sessionId) const {
  const auto session = sessions_.find(sessionId);
  if (session == sessions_.end()) return std::nullopt;
  return session->second;
}

std::optional<SessionRecord> SessionRegistry::resolve(uint64_t sessionId, const DocumentIdentity &document) const {
  const auto session = find(sessionId);
  if (!session || !document.mainFrame || session->document.browserId != document.browserId ||
      session->document.frameId != document.frameId || session->document.origin != document.origin) return std::nullopt;
  return session;
}

bool SessionRegistry::allows(uint64_t sessionId, const std::string &capability, bool managerOnly) const {
  const auto session = find(sessionId);
  if (!session) return false;
  const auto &app = apps_.at(session->appId);
  if (managerOnly && !app.authorization.platformManager) return false;
  if (capability.empty()) return true;
  return app.identity.declaredCapabilities.contains(capability) &&
    app.authorization.policyCapabilities.contains(capability) &&
    app.authorization.grantedCapabilities.contains(capability);
}
} // namespace alkaid
