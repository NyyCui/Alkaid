#ifndef CEF_SCHEME_HANDLER_FACTORY_H
#define CEF_SCHEME_HANDLER_FACTORY_H
#include "include/cef_scheme.h"

namespace alkaid {
class SchemeHandlerFactory : public CefSchemeHandlerFactory {
  IMPLEMENT_REFCOUNTING(SchemeHandlerFactory);

public:
  SchemeHandlerFactory() = default;

  SchemeHandlerFactory(const SchemeHandlerFactory &) = delete;

  SchemeHandlerFactory &operator=(const SchemeHandlerFactory &) = delete;

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString &scheme_name,
                                       CefRefPtr<CefRequest> request) override;
};
} // namespace alkaid
#endif // CEF_SCHEME_HANDLER_FACTORY_H
