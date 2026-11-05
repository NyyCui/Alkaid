#include "window/browser_view_delegate.h"

#include "include/views/cef_window.h"
#include "window_delegate.h"

namespace alkaid {
bool BrowserViewDelegate::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowserView> popup_browser_view, bool is_devtools) {
  CefWindow::CreateTopLevelWindow(new WindowDelegate(popup_browser_view));
  return true;
}

cef_runtime_style_t BrowserViewDelegate::GetBrowserRuntimeStyle() {
  // return CEF_RUNTIME_STYLE_CHROME;
  return CEF_RUNTIME_STYLE_ALLOY;
}
} // namespace alkaid
