#ifndef ALKAID_WINDOW_MANAGER_H
#define ALKAID_WINDOW_MANAGER_H

#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

namespace alkaid {
/**
 * @brief 全局窗口管理器
 *
 * 统一登记应用中所有承载浏览器实例的窗口，接管窗口关闭事件：
 * - 普通应用窗口：直接关闭；
 * - desktop 主应用窗口：阻止本次关闭，通知其 JS 弹出确认框；
 *   JS 通过系统调用 ExitApp 回传用户选择，选择“退出”时由本管理器统一关闭全部窗口并退出。
 */
class WindowManager {
  struct WindowRecord {
    CefRefPtr<CefWindow> window;
    CefRefPtr<CefBrowserView> browserView;
    std::string url;
    bool isDesktop = false;
  };

  // key：由本管理器分配并通过 CefView::SetID 写入窗口的唯一 id
  std::map<int, WindowRecord> windows_;
  std::set<int> browserIds_;         ///< 当前存活的浏览器 id
  bool exiting_ = false;             ///< 是否已进入统一退出流程
  std::atomic<int> nextWindowId_{1}; ///< 窗口唯一 id 分配器（从 1 开始，避开默认 0）

  WindowManager() = default;

  WindowManager(const WindowManager &) = delete;

  WindowManager &operator=(const WindowManager &) = delete;

  // 通知 desktop 应用的 JS 弹出退出确认框
  void notifyDesktopCloseRequest(const WindowRecord &record);

  // 所有窗口与浏览器均已关闭时结束消息循环
  void checkQuitMessageLoop();

public:
  static WindowManager &instance();

  // 登记新窗口
  void addWindow(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> browserView,
    const std::string &url, bool isDesktop
  );

  // 窗口销毁时移除登记
  void removeWindow(int windowId);

  // 处理窗口关闭请求，返回是否允许本次关闭
  bool handleRequestClose(CefRefPtr<CefWindow> window);

  // 浏览器生命周期回调
  void onBrowserCreated(CefRefPtr<CefBrowser> browser);

  void onBrowserClosed(CefRefPtr<CefBrowser> browser);

  // 统一关闭所有窗口并退出程序（须在 UI 线程调用）
  void exitApplication();

  // 注册“窗口管理”模块及 ExitApp 系统调用
  static void registerWindowManagerModule();
};

/** @brief 将统一退出操作投递到 UI 线程执行 */
class ExitAppTask: public CefTask {
  IMPLEMENT_REFCOUNTING(ExitAppTask);

public:
  ExitAppTask() = default;

  void Execute() override;
};

} // namespace alkaid

#endif // ALKAID_WINDOW_MANAGER_H
