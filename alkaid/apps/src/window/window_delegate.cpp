#include "window/window_delegate.h"

#include "include/cef_app.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "window/window_manager.h"

namespace alkaid {
WindowDelegate::WindowDelegate(CefRefPtr<CefBrowserView> browser_view,
  std::string url, bool isDesktop
)
  : browser_view_(browser_view)
  , url_(std::move(url))
  , is_desktop_(isDesktop) {}

// 窗口创建成功
void WindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->AddChildView(browser_view_);
  window->Show();
  browser_view_->RequestFocus();
  window->SetTitle(L"这是我的窗口标题");
  // 统一登记到窗口管理器
  WindowManager::instance().addWindow(window, browser_view_, url_, is_desktop_);
  // window->CenterWindow(CefSize(800, 600));
}
// 窗口销毁成功
void WindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  // 从窗口管理器注销
  WindowManager::instance().removeWindow(window->GetID());
  // CefQuitMessageLoop();
}
// 设置窗口位置和大小
CefRect WindowDelegate::GetInitialBounds(CefRefPtr<CefWindow> window) {
  CefRefPtr<CefDisplay> display = CefDisplay::GetPrimaryDisplay();
  CefRect rect = display->GetBounds();
  rect.x = (rect.width - 800) / 2;
  rect.y = (rect.height - 600) / 2;
  rect.width = 800;
  rect.height = 600;
  return rect;
}

bool WindowDelegate::CanClose(CefRefPtr<CefWindow> window) {
  // 关闭事件统一交给窗口管理器处理
  return WindowManager::instance().handleRequestClose(window);
}

cef_runtime_style_t WindowDelegate::GetWindowRuntimeStyle() {
  // return CEF_RUNTIME_STYLE_CHROME;
  return CEF_RUNTIME_STYLE_ALLOY;
}

/*bool WindowDelegate::OnAccelerator(CefRefPtr<CefWindow> window,
                                   int command_id) {
  // F12对应的accelerator command id： IDC_DEV_TOOLS = 40012
  constexpr int IDC_DEV_TOOLS = 40012;
  if (command_id == IDC_DEV_TOOLS) {
    // 返回true：消费该快捷键，不再向下传递，直接阻止内部崩溃逻辑
    return true;
  }
  return false;
}*/
} // namespace alkaid
