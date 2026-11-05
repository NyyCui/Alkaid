#ifndef CEF_JS_DIALOG_HANDLER_H
#define CEF_JS_DIALOG_HANDLER_H
#include "include/cef_base.h"
#include "include/cef_jsdialog_handler.h"

namespace alkaid {
class JSDialogHandler : public CefJSDialogHandler {
  IMPLEMENT_REFCOUNTING(JSDialogHandler);

public:
  bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString &origin_url,
                  JSDialogType dialog_type, const CefString &message_text,
                  const CefString &default_prompt_text,
                  CefRefPtr<CefJSDialogCallback> callback,
                  bool &suppress_message) override;

  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser,
                            const CefString &message_text, bool is_reload,
                            CefRefPtr<CefJSDialogCallback> callback) override;
};
} // namespace alkaid
#endif // CEF_JS_DIALOG_HANDLER_H
