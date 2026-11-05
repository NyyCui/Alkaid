/** @file app_context.h 每个应用等待自己的持久化 profile 就绪后创建窗口。 */
#ifndef ALKAID_APP_CONTEXT_H
#define ALKAID_APP_CONTEXT_H
#include "include/cef_request_context_handler.h"
#include "resource_service.h"

namespace alkaid {
class AppRuntime;
class AppContext : public CefRequestContextHandler {
  IMPLEMENT_REFCOUNTING(AppContext);
public:
  AppContext(AppRuntime *runtime, std::string appId) : runtime_(runtime), appId_(std::move(appId)) {}
  AppContext(const AppContext &) = delete;
  AppContext &operator=(const AppContext &) = delete;
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext>) override;
private:
  AppRuntime *runtime_;
  std::string appId_;
};
} // namespace alkaid
#endif // ALKAID_APP_CONTEXT_H
