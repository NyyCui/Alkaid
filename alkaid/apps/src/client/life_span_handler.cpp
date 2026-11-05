#include "life_span_handler.h"

#include "include/cef_app.h"
#include "window/window_manager.h"

namespace alkaid {
LifeSpanHandler::LifeSpanHandler() = default;

// 禁用window.open, target="_blank"
bool LifeSpanHandler::OnBeforePopup(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
    const CefString &target_url, const CefString &target_frame_name,
    WindowOpenDisposition target_disposition, bool user_gesture,
    const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo,
    CefRefPtr<CefClient> &client, CefBrowserSettings &settings,
    CefRefPtr<CefDictionaryValue> &extra_info, bool *no_javascript_access) {
  return true;
}

void LifeSpanHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_map_[browser->GetIdentifier()] = browser;
  // 交由全局窗口管理器统计存活浏览器
  WindowManager::instance().onBrowserCreated(browser);
}

void LifeSpanHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  // Remove browser from our tracking map
  browser_map_.erase(browser->GetIdentifier());

  // 退出时机统一由窗口管理器判定（所有窗口与浏览器都关闭后才退出）
  WindowManager::instance().onBrowserClosed(browser);
}
} // namespace alkaid
