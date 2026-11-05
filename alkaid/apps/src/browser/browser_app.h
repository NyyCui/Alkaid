#ifndef ALKAID_BROWSER_APP_H
#define ALKAID_BROWSER_APP_H
#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace alkaid {
class BrowserApp : public CefApp, public CefBrowserProcessHandler {
  IMPLEMENT_REFCOUNTING(BrowserApp);

public:
  BrowserApp();
  BrowserApp(const BrowserApp &) = delete;
  BrowserApp &operator=(const BrowserApp &) = delete;
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;

  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnContextInitialized() override;

  void OnBeforeCommandLineProcessing(
    const CefString &process_type,
    CefRefPtr<CefCommandLine> command_line
  ) override;

  static void init();
};
} // namespace alkaid

#endif // ALKAID_BROWSER_APP_H
