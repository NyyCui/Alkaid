#include "window/window_manager.h"

#include "alkaid_log/logging.h"
#include "alkaid_utils/module_registry.h"
#include "sys_event/sys_event.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "sys_func/sys_func.h"

namespace alkaid {

void WindowManager::notifyDesktopCloseRequest(const WindowRecord &record) {
  const auto browser = record.browserView->GetBrowser();
  if (!browser) {
    return;
  }
  const auto frame = browser->GetMainFrame();
  if (!frame) {
    return;
  }
  // 通过 sysPublish 通道，向 desktop 页面推送“关闭请求”事件，由其 JS 创建对话框
  const auto msg = CefProcessMessage::Create("sysPublish");
  msg->GetArgumentList()->SetInt(0, SysEvent::BeforeAppDestroy);
  msg->GetArgumentList()->SetString(1, "");
  frame->SendProcessMessage(PID_RENDERER, msg);
  LOGGER_DEBUG("已通知 desktop 应用弹出退出确认框");
}

void WindowManager::checkQuitMessageLoop() {
  if (browserIds_.empty() && windows_.empty()) {
    LOGGER_DEBUG("所有窗口与浏览器均已关闭，退出消息循环");
    CefQuitMessageLoop();
  }
}

WindowManager &WindowManager::instance() {
  static WindowManager manager;
  return manager;
}

void WindowManager::addWindow(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> browserView,
  const std::string &url, const bool isDesktop
) {
  if (!window) {
    return;
  }
  // 新建窗口默认 ID 均为 0，这里由管理器分配唯一 id 并写回窗口，
  // 之后即可通过 CefView::GetID() 读回作为登记表的键
  const int id = nextWindowId_.fetch_add(1);
  window->SetID(id);
  windows_[id] = WindowRecord{window, browserView, url, isDesktop};
  LOGGER_DEBUG("登记窗口：id={}、url={}、isDesktop={}", id, url, isDesktop);
}

void WindowManager::removeWindow(const int windowId) {
  windows_.erase(windowId);
  checkQuitMessageLoop();
}

bool WindowManager::handleRequestClose(CefRefPtr<CefWindow> window) {
  if (!window) {
    return true;
  }
  const auto it = windows_.find(window->GetID());
  if (it == windows_.end()) {
    // 未登记的窗口：放行关闭
    return true;
  }

  // 已进入统一退出流程：所有窗口一律放行，避免重复弹窗
  if (exiting_) {
    return true;
  }

  if (it->second.isDesktop) {
    // desktop 主应用：阻止本次关闭，通知 JS 弹出确认框
    notifyDesktopCloseRequest(it->second);
    return false;
  }

  // 普通应用：正常关闭其浏览器
  if (const auto browser = it->second.browserView->GetBrowser()) {
    return browser->GetHost()->TryCloseBrowser();
  }
  return true;
}

void WindowManager::onBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browserIds_.insert(browser->GetIdentifier());
}

void WindowManager::onBrowserClosed(CefRefPtr<CefBrowser> browser) {
  browserIds_.erase(browser->GetIdentifier());
  checkQuitMessageLoop();
}

void WindowManager::exitApplication() {
  exiting_ = true;
  // 先快照窗口列表，避免 Close() 回调移除元素导致迭代器失效
  std::vector<CefRefPtr<CefWindow>> targets;
  targets.reserve(windows_.size());
  for (const auto &[id, record]: windows_) {
    if (record.window && record.window->IsValid()) {
      targets.push_back(record.window);
    }
  }
  for (const auto &win: targets) {
    win->Close();
  }
  LOGGER_DEBUG("已发起统一退出：关闭 {} 个窗口", targets.size());
}

void WindowManager::registerWindowManagerModule() {
  ModuleRegistry::registerModule({
    "WindowManager",
    "",
    {"Log"},
    []() -> tl::expected<void, std::string> {
      SysFunc::registerSysInvoke(
        "ExitApp",
        []() -> SysInvokeResult {
          CefPostTask(TID_UI, new ExitAppTask());
          // cancel：保持 desktop 窗口打开，无需处理
          return SysInvokeResult::ok();
        });
      return {};
    },
    [] {}
  });
}

void ExitAppTask::Execute() {
  WindowManager::instance().exitApplication();
}

} // namespace alkaid
