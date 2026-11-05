/** @file close_coordinator.cpp 应用保存协商与平台退出屏障，拒绝或超时均保留窗口。 */
#include "app_runtime.h"
#include "package_manager.h"
#include "function_task.h"
#include "file_access.h"
#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
namespace alkaid {
void AppRuntime::emit(Entry &entry,const std::string &event,const Json &payload) {
  if (!entry.browser || !entry.browser->IsValid()) return;
  const auto value=Json{{"event",event},{"payload",payload}}.dump();
  entry.browser->GetMainFrame()->ExecuteJavaScript("globalThis.dispatchEvent(new CustomEvent('alkaid:runtime-event',{detail:"+value+"}));",
    "alkaid://"+entry.definition.originKey+"/index.html",0);
}
void AppRuntime::finishClose(Entry &entry) {
  entry.closeApproved=true; entry.state="closing";
  hideWorkspace(entry);
  const auto appId=entry.definition.appId; const auto alive=alive_;
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive,appId] {
    if (!alive->load()) return;
    const auto found=entries_.find(appId);
    if (found!=entries_.end() && found->second.closeApproved && found->second.browser)
      found->second.browser->GetHost()->CloseBrowser(true);
  }),100);
}
void AppRuntime::requestClose(Entry &entry,const std::string &reason) {
  if (entry.state=="stopped" || entry.state=="unavailable" || entry.closeApproved || !entry.closeTicket.empty()) return;
  entry.closeMessage.clear();
  if (!entry.closeHandler || !entry.browser) {
    entry.closeReady=true;
    if (!exitPending_) finishClose(entry);
    return;
  }
  entry.closeTicket=FileAccess::randomId();
  if (entry.closeTicket.empty()) { if (exitPending_) cancelShutdown("无法启动关闭协商，请稍后重试"); else entry.closeMessage="无法启动关闭协商，请稍后重试"; return; }
  entry.closeReady=false; entry.state="closing";
  const auto ticket=entry.closeTicket, appId=entry.definition.appId; const auto alive=alive_;
  emit(entry,"window.closeRequested",{{"requestId",ticket},{"reason",reason}});
  focus(entry);
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive,appId,ticket] {
    if (!alive->load()) return;
    const auto found=entries_.find(appId);
    if (found==entries_.end() || found->second.closeTicket!=ticket || found->second.closeReady) return;
    if (exitPending_) cancelShutdown("应用未在 30 秒内确认关闭，平台退出已取消");
    else cancelClose(found->second,"应用未在 30 秒内确认关闭，窗口已保留");
  }),30000);
}
void AppRuntime::cancelClose(Entry &entry,const std::string &message) {
  if (!entry.closeTicket.empty()) emit(entry,"window.closeCancelled",{{"requestId",entry.closeTicket},{"message",message}});
  entry.closeTicket.clear(); entry.closeReady=false; entry.closeParticipant=false;
  if (!entry.closeApproved && entry.state=="closing") entry.state=entry.browser?"running":"starting";
  entry.closeMessage=message;
}
void AppRuntime::cancelShutdown(const std::string &message) {
  exitPending_=false;
  for (auto &[id,entry]:entries_) if (entry.closeParticipant && !entry.closeApproved) cancelClose(entry,message);
  const auto desktop=entries_.find("org.alkaid.desktop");
  if (desktop!=entries_.end()) emit(desktop->second,"runtime.exitCancelled",{{"message",message}});
}
void AppRuntime::finishShutdown() {
  if (!exitPending_) return;
  for (auto &[id,entry]:entries_) {
    if (!entry.closeParticipant || entry.closeReady || entry.state=="stopped" || entry.state=="unavailable") continue;
    if (entry.overlay) focus(entry);
    return;
  }
  const auto alive=alive_;
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive] {
    if (!alive->load() || !exitPending_) return;
    for (const auto &[id,entry]:entries_) if (entry.closeParticipant && !entry.closeReady && entry.state!="stopped" && entry.state!="unavailable") return;
    exitPending_=false; shuttingDown_=true;
    if (packages_) packages_->stop();
    for (auto &[id,entry]:entries_)
      if (!entry.definition.desktop && entry.state!="stopped" && entry.state!="unavailable") finishClose(entry);
    maybeQuit();
  }),100);
}
void AppRuntime::shutdown() {
  if (shuttingDown_ || exitPending_) return;
  exitPending_=true;
  for (auto &[id,entry]:entries_) {
    if (entry.definition.desktop || entry.state=="stopped" || entry.state=="unavailable" || entry.closeApproved) continue;
    entry.closeParticipant=true; requestClose(entry,"platform");
    if (!exitPending_) return;
  }
  finishShutdown();
}
bool AppRuntime::requestWindowClose(const std::string &appId) {
  const auto found=entries_.find(appId); if (found==entries_.end()) return true;
  auto &entry=found->second;
  if (entry.closeApproved || shuttingDown_) return true;
  if (entry.definition.desktop) shutdown(); else requestClose(entry,"window");
  return false;
}
RuntimeResult<Json> AppRuntime::windowCall(const std::string &appId,const std::string &method,const Json &params) {
  const auto found=entries_.find(appId);
  if (found==entries_.end() || !found->second.browser) return RuntimeError{"APP_NOT_RUNNING","应用窗口尚未建立"};
  auto &entry=found->second;
  if (method == "window.minimize" || method == "window.toggleMaximize") {
    const auto view = CefBrowserView::GetForBrowser(entry.browser);
    const auto window = view ? view->GetWindow() : nullptr;
    if (!window) return RuntimeError{"APP_NOT_RUNNING", "窗口尚未建立"};
    if (method == "window.minimize") window->Minimize();
    else if (window->IsMaximized()) window->Restore();
    else window->Maximize();
    return Json(nullptr);
  }
  if (method=="window.setCloseHandler") {
    const bool enabled=params["enabled"].get<bool>();
    if (!enabled) resetCloseHandler(appId);
    entry.closeHandler=enabled;
    if (enabled && exitPending_ && !entry.definition.desktop && !entry.closeApproved) {
      entry.closeParticipant=true; entry.closeReady=false; requestClose(entry,"platform");
    }
    return Json(nullptr);
  }
  if (entry.closeTicket.empty() || entry.closeTicket!=params["requestId"].get<std::string>() || entry.closeReady)
    return RuntimeError{"REQUEST_EXPIRED","关闭请求已失效或不属于当前应用"};
  if (!params["allow"].get<bool>()) {
    if (exitPending_) cancelShutdown("应用取消了关闭，平台继续运行"); else cancelClose(entry,"已取消关闭");
  } else {
    entry.closeReady=true;
    if (exitPending_) finishShutdown(); else finishClose(entry);
  }
  return Json(nullptr);
}
void AppRuntime::resetCloseHandler(const std::string &appId) {
  const auto found=entries_.find(appId); if (found==entries_.end()) return;
  auto &entry=found->second;
  if (!entry.closeApproved && !shuttingDown_ && !entry.closeTicket.empty()) {
    if (exitPending_) cancelShutdown("应用页面发生变化，平台退出已取消"); else cancelClose(entry,"页面已变化，关闭请求已取消");
  }
  entry.closeHandler=false;
}
}
