/** @file app_context.cpp 将异步上下文就绪通知交给应用运行管理器。 */
#include "app_context.h"
#include "app_runtime.h"
#include "include/wrapper/cef_helpers.h"

namespace alkaid {
void AppContext::OnRequestContextInitialized(CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  runtime_->contextReady(appId_, context);
}
} // namespace alkaid
