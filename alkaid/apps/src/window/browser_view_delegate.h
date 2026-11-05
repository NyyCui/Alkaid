#ifndef CEF_PROJECT_BROWSER_VIEW_DELEGATE_H
#define CEF_PROJECT_BROWSER_VIEW_DELEGATE_H
#include "include/views/cef_browser_view_delegate.h"

namespace alkaid {
class BrowserViewDelegate : public CefBrowserViewDelegate {
  IMPLEMENT_REFCOUNTING(BrowserViewDelegate);

  BrowserViewDelegate(const BrowserViewDelegate &) = delete;

  BrowserViewDelegate &operator=(const BrowserViewDelegate &) = delete;

public:
  BrowserViewDelegate() = default;

  // 当前页面弹出新窗口时此方法被执行
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;

  cef_runtime_style_t GetBrowserRuntimeStyle() override;
};
} // namespace alkaid
#endif // CEF_PROJECT_BROWSER_VIEW_DELEGATE_H
