#ifndef CEF_PROJECT_WINDOW_DELEGATE_H
#define CEF_PROJECT_WINDOW_DELEGATE_H
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window_delegate.h"

#include <string>

namespace alkaid {
class WindowDelegate : public CefWindowDelegate {
  IMPLEMENT_REFCOUNTING(WindowDelegate);
  WindowDelegate(const WindowDelegate &) = delete;

  WindowDelegate &operator=(const WindowDelegate &) = delete;

  CefRefPtr<CefBrowserView> browser_view_;

  // 该窗口加载的应用 URL
  std::string url_;
  // 是否为 desktop 主应用窗口
  bool is_desktop_;

public:
  explicit WindowDelegate(CefRefPtr<CefBrowserView> browser_view,
    std::string url = "", bool isDesktop = false
  );

  // 窗口创建成功
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;

  // 窗口销毁成功
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;

  // 设置窗口位置和大小
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override;

  bool CanClose(CefRefPtr<CefWindow> window) override;

  cef_runtime_style_t GetWindowRuntimeStyle() override;
};
} // namespace alkaid
#endif // CEF_PROJECT_WINDOW_DELEGATE_H
