#include "alkaid_utils/process_util.h"
#include "browser/browser_app.h"
#include "browser/other_app.h"
#include "render/renderer_app.h"
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
  CefMainArgs main_args(hInstance);

  void *sandbox_info = nullptr;

#if defined(CEF_USE_SANDBOX)
  // Manage the life span of the sandbox information object. This is necessary
  // for sandbox support on Windows. See cef_sandbox_win.h for complete details.
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif
  CefRefPtr<CefApp> app;

  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  if (!command_line->HasSwitch("type")) {
    app = new alkaid::BrowserApp();
  } else if (command_line->GetSwitchValue("type") == "renderer") {
    app = new alkaid::RendererApp();
  } else {
    app = new alkaid::OtherApp();
  }

  const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  CefString(&settings.locale) = "zh-CN";
  CefString(&settings.root_cache_path) = (alkaid::ProcessUtil::homeDir() / "data").string();
  CefString(&settings.cache_path) = (alkaid::ProcessUtil::homeDir() / "data/Cache").string();
  CefString(&settings.log_file) = (alkaid::ProcessUtil::homeDir() / "logs/cef.log").string();
  settings.log_severity = LOGSEVERITY_INFO;
  settings.log_items = static_cast<cef_log_items_t>(
    LOG_ITEMS_FLAG_PROCESS_ID | LOG_ITEMS_FLAG_THREAD_ID |
    LOG_ITEMS_FLAG_TIME_STAMP);

  if (!sandbox_info) settings.no_sandbox = true;

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();

  CefShutdown();
  return 0;
}
