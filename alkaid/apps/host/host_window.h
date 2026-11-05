/** @file host_window.h 桌面与普通应用共用的原生窗口委托。 */
#ifndef ALKAID_HOST_WINDOW_H
#define ALKAID_HOST_WINDOW_H
#include "package_types.h"
#include "include/views/cef_window_delegate.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_browser_view.h"
#include <optional>
#include "tray_icon.h"

namespace alkaid {
class AppRuntime;
class HostWindow : public CefWindowDelegate, public CefBrowserViewDelegate {
  IMPLEMENT_REFCOUNTING(HostWindow);
public:
  HostWindow(AppRuntime *runtime, std::string appId, std::string title, WindowOptions options, std::optional<CefRect> bounds, bool desktop, std::filesystem::path iconRoot)
    : runtime_(runtime), appId_(std::move(appId)), title_(std::move(title)), options_(options), savedBounds_(bounds), desktop_(desktop), iconRoot_(std::move(iconRoot)) {}
  HostWindow(const HostWindow &) = delete;
  HostWindow &operator=(const HostWindow &) = delete;
  void setBrowserView(CefRefPtr<CefBrowserView> view) { view_ = view; }
  void OnWindowCreated(CefRefPtr<CefWindow>) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect &) override;
  void OnWindowClosing(CefRefPtr<CefWindow>) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override;
  bool IsFrameless(CefRefPtr<CefWindow>) override { return desktop_; }
  bool CanClose(CefRefPtr<CefWindow>) override;
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(options_.width, options_.height); }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override;
  bool CanResize(CefRefPtr<CefWindow>) override { return options_.resizable; }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
private:
  AppRuntime *runtime_;
  std::string appId_;
  std::string title_;
  const WindowOptions options_;
  const std::optional<CefRect> savedBounds_;
  CefSize minimumSize_{options_.minWidth, options_.minHeight};
  bool shown_ = false;
  bool desktop_;
  std::filesystem::path iconRoot_;
  std::unique_ptr<TrayIcon> tray_;
  CefRefPtr<CefBrowserView> view_;
};
} // namespace alkaid
#endif // ALKAID_HOST_WINDOW_H
