#ifndef CEF_REQUEST_HANDLER_H
#define CEF_REQUEST_HANDLER_H
#include "include/cef_request_handler.h"

namespace alkaid {
class RequestHandler : public CefRequestHandler {
  IMPLEMENT_REFCOUNTING(RequestHandler);

public:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
};
} // namespace alkaid
#endif // CEF_REQUEST_HANDLER_H
