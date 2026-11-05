#include "browser_app.h"

#include "window/window_delegate.h"
#include "alkaid_log/logging.h"
#include "alkaid_utils/process_util.h"
#include "window/browser_view_delegate.h"
#include "client/client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "scheme_handler_factory.h"
#include "test.h"
#include "alkaid_utils/module_registry.h"
#include "app_manage/app_manage.h"
#include "window/window_manager.h"

namespace alkaid {

BrowserApp::BrowserApp() {
  init();
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler() {
  return this;
}

void BrowserApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(RESOURCES_SCHEME, CEF_SCHEME_OPTION_STANDARD);
}

void BrowserApp::OnBeforeCommandLineProcessing(
  const CefString &process_type,
  CefRefPtr<CefCommandLine> command_line
) {
}

void BrowserApp::init() {
  Log::registerLoggingModule();
  AppManage::registerAppManageModule();
  WindowManager::registerWindowManagerModule();
  auto init_all = ModuleRegistry::initAll("Browser");
  t();
}

void BrowserApp::OnContextInitialized() {
  CefRefPtr<CefSchemeHandlerFactory> factory(new SchemeHandlerFactory());
  // CefAddCrossOriginWhitelistEntry("alkaid://desktop", "http", "desktop", false);

  const std::string scheme = RESOURCES_SCHEME;
  // 注册到全局请求上下文
  CefRegisterSchemeHandlerFactory(scheme, "", factory);

  const auto url = scheme + "://qq-jt.com-desktop";
  CefBrowserSettings settings;
  CefRefPtr<CefClient> client(new Client());
  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
    client, url, settings, nullptr, nullptr, new BrowserViewDelegate());
  // desktop 主应用窗口，标记 isDesktop = true
  CefWindow::CreateTopLevelWindow(new WindowDelegate(browser_view, url, false));
}

} // namespace alkaid
