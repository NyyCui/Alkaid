/** @file host_app.cpp 应用运行管理与按宿主登记来源注入 Renderer 桥接。 */
#include "host_app.h"
#include "url_policy.h"
#include "include/wrapper/cef_helpers.h"
#include <windows.h>

namespace alkaid {
void HostApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme("alkaid", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
    CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void HostApp::OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> command) {
  // 生产入口拒绝破坏沙箱或同源边界的调试开关。
  command->RemoveSwitch("no-sandbox");
  command->RemoveSwitch("disable-web-security");
  command->RemoveSwitch("ignore-certificate-errors");
  command->AppendSwitch("disable-background-networking");
}

void HostApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  std::string error;
  runtime_ = std::make_unique<AppRuntime>(root_, dataRoot_);
  if (!runtime_->initialize(error)) {
    MessageBoxW(nullptr, CefString(error).ToWString().c_str(), L"瑶光台启动失败", MB_OK | MB_ICONERROR);
    CefQuitMessageLoop();
    return;
  }
}

void HostApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra) {
  if (extra && extra->GetType("originKey") == VTYPE_STRING)
    origins_[browser->GetIdentifier()] = extra->GetString("originKey").ToString();
}

void HostApp::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) { origins_.erase(browser->GetIdentifier()); }

void HostApp::OnWebKitInitialized() {
  CefMessageRouterConfig config;
  config.js_query_function = "__alkaidQuery";
  config.js_cancel_function = "__alkaidCancel";
  router_ = CefMessageRouterRendererSide::Create(config);
}

void HostApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  // 当前没有实时点对点/QUIC 能力，禁止从页面创建绕过资源请求处理器的网络对象。
  const auto global=context->GetGlobal();
  for(const auto *name:{"RTCPeerConnection","webkitRTCPeerConnection","RTCIceTransport","WebTransport"})
    global->SetValue(name,CefV8Value::CreateUndefined(),static_cast<cef_v8_propertyattribute_t>(V8_PROPERTY_ATTRIBUTE_READONLY|V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  const auto origin = origins_.find(browser->GetIdentifier());
  if (router_ && frame->IsMain() && origin != origins_.end() && UrlPolicy::isApp(frame->GetURL(), origin->second))
    router_->OnContextCreated(browser, frame, context);
}

void HostApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  if (router_) router_->OnContextReleased(browser, frame, context);
}

bool HostApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                      CefProcessId source, CefRefPtr<CefProcessMessage> message) {
  return router_ && source == PID_BROWSER && router_->OnProcessMessageReceived(browser, frame, source, message);
}
} // namespace alkaid
