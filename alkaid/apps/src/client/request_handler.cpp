#include "request_handler.h"

namespace alkaid {
// 禁用<iframe>, <frame>
bool RequestHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefRequest> request,
                                    bool user_gesture, bool is_redirect) {
  // 只允许主框架导航，其他全部取消
  if (!frame->IsMain()) {
    return true; // 取消子框架加载
  }
  return false; // 允许主框架
}
} // namespace alkaid
