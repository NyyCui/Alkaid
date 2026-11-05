#ifndef CEF_PROJECT_CLIENT_H
#define CEF_PROJECT_CLIENT_H
#include "dialog_handler.h"
#include "include/cef_client.h"
#include "js_dialog_handler.h"
#include "request_handler.h"

namespace alkaid {
class Client : public CefClient, public CefKeyboardHandler {
  IMPLEMENT_REFCOUNTING(Client);

  CefRefPtr<CefLifeSpanHandler> life_span_handler_;
  CefRefPtr<RequestHandler> request_handler_;
  CefRefPtr<JSDialogHandler> jsdialog_handler_;
  CefRefPtr<DialogHandler> dialog_handler_;
  void sendSysInvokeResult(CefRefPtr<CefFrame> frame, int32_t rid, int32_t code, bool reserved, const std::string &result);

public:
  Client();

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;

  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override;

  CefRefPtr<CefRequestHandler> GetRequestHandler() override;

  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override;

  CefRefPtr<CefDialogHandler> GetDialogHandler() override;

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent &event,
                     cef_event_handle_t os_event,
                     bool *is_keyboard_shortcut) override;

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
};
} // namespace alkaid
#endif // CEF_PROJECT_CLIENT_H
