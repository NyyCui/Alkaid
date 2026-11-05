#ifndef ALKAID_RENDERER_APP_H
#define ALKAID_RENDERER_APP_H

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"

namespace alkaid {
class RendererApp : public CefApp, public CefRenderProcessHandler {
  IMPLEMENT_REFCOUNTING(RendererApp);

public:
  RendererApp();
  RendererApp(const RendererApp &) = delete;
  RendererApp &operator=(const RendererApp &) = delete;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;

  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
};
} // namespace alkaid
#endif // ALKAID_RENDERER_APP_H
