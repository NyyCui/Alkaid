#include "client.h"

#include "alkaid_log/logging.h"
#include "life_span_handler.h"
#include "sys_func/sys_func.h"

namespace alkaid {

Client::Client()
  : life_span_handler_(new LifeSpanHandler())
  , request_handler_(new RequestHandler())
  , jsdialog_handler_(new JSDialogHandler())
  , dialog_handler_(new DialogHandler()) {}

CefRefPtr<CefLifeSpanHandler> Client::GetLifeSpanHandler() {
  return life_span_handler_;
}

CefRefPtr<CefKeyboardHandler> Client::GetKeyboardHandler() {
  return this;
}

CefRefPtr<CefRequestHandler> Client::GetRequestHandler() {
  return request_handler_;
}

CefRefPtr<CefJSDialogHandler> Client::GetJSDialogHandler() {
  return jsdialog_handler_;
}

CefRefPtr<CefDialogHandler> Client::GetDialogHandler() {
  return dialog_handler_;
}

bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
  const CefKeyEvent &event,
  cef_event_handle_t os_event,
  bool *is_keyboard_shortcut
) {
  LOGGER_DEBUG("键码：{}", event.windows_key_code);
  return false;
}

void Client::sendSysInvokeResult(CefRefPtr<CefFrame> frame, int32_t rid, int32_t code, bool reserved, const std::string &result) {
  LOGGER_DEBUG("发送系统调用处理结果：rid：{}、code：{}、reserved：{}、result：{}", rid, code, reserved, result);
  const auto msg = CefProcessMessage::Create("sysInvokeResult");
  msg->GetArgumentList()->SetInt(0, rid);
  msg->GetArgumentList()->SetInt(1, code);
  msg->GetArgumentList()->SetBool(2, reserved);
  msg->GetArgumentList()->SetString(3, result);
  frame->SendProcessMessage(PID_RENDERER, msg);
}

bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
  CefRefPtr<CefFrame> frame,
  CefProcessId source_process,
  CefRefPtr<CefProcessMessage> message
) {
  if (message->GetName() == "sysInvoke") {
    const auto args = message->GetArgumentList();
    const int32_t rid = args->GetInt(0);
    const int32_t code = args->GetInt(1);
    const auto param = args->GetSize() == 3 ? std::optional(args->GetString(2).ToString()) : std::nullopt;
    LOGGER_DEBUG("收到参数：{}", param.value_or("无参数"));
    if (auto result = SysFunc::invoke(code, param)) {
      sendSysInvokeResult(frame, rid, code, true, nlohmann::json(result.value()).dump());
    } else {
      sendSysInvokeResult(frame, rid, code, false, result.error());
    }
    return true;
  }
  return false;
}
} // namespace alkaid
