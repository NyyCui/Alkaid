/** @file host_app.h 新宿主的 Browser 初始化及 Renderer 消息路由。 */
#ifndef ALKAID_HOST_APP_H
#define ALKAID_HOST_APP_H
#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"
#include "app_runtime.h"
#include <filesystem>

namespace alkaid {
class HostApp : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
  IMPLEMENT_REFCOUNTING(HostApp);
public:
  HostApp() = default;
  HostApp(const HostApp &) = delete;
  HostApp &operator=(const HostApp &) = delete;
  void setRoot(std::filesystem::path root) { root_ = std::move(root); }
  void setDataRoot(std::filesystem::path root) { dataRoot_ = std::move(root); }
  void releaseContext() { runtime_.reset(); }
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar>) override;
  void OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine>) override;
  void OnContextInitialized() override;
  void OnWebKitInitialized() override;
  void OnBrowserCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefDictionaryValue>) override;
  void OnBrowserDestroyed(CefRefPtr<CefBrowser>) override;
  void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
  void OnContextReleased(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context>) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;
private:
  std::filesystem::path root_;
  std::filesystem::path dataRoot_;
  std::unique_ptr<AppRuntime> runtime_;
  std::map<int, std::string> origins_;
  CefRefPtr<CefMessageRouterRendererSide> router_;
};
} // namespace alkaid
#endif // ALKAID_HOST_APP_H
