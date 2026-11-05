#include "js_dialog_handler.h"

namespace alkaid {
// 禁用 alert, confirm, prompt
bool JSDialogHandler::OnJSDialog(CefRefPtr<CefBrowser> browser,
                                 const CefString &origin_url,
                                 JSDialogType dialog_type,
                                 const CefString &message_text,
                                 const CefString &default_prompt_text,
                                 CefRefPtr<CefJSDialogCallback> callback,
                                 bool &suppress_message) {
  suppress_message = true; // 关键：抑制弹窗显示
  return false;            // 使用默认处理（但消息已被抑制）
}

// 页面关闭/刷新确认框 (onbeforeunload)
bool JSDialogHandler::OnBeforeUnloadDialog(
    CefRefPtr<CefBrowser> browser, const CefString &message_text,
    bool is_reload, CefRefPtr<CefJSDialogCallback> callback) {
  return true; // 阻止页面离开确认弹窗
}
} // namespace alkaid
