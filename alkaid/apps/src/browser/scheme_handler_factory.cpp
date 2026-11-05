#include "scheme_handler_factory.h"

#include "alkaid_utils/process_util.h"
#include "render/resource_handler.h"
namespace alkaid {
CefRefPtr<CefResourceHandler> SchemeHandlerFactory::Create(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    const CefString &scheme_name, CefRefPtr<CefRequest> request) {
  return new ResourceHandler(ProcessUtil::homeDir() / APP_PATH);
}
} // namespace alkaid
