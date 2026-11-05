/** @file main.cpp 通过官方 bootstrap 启动带沙箱的宿主及 CEF 子进程。 */
#include "host_app.h"
#include "include/cef_sandbox_win.h"
#include <windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <string>

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPWSTR, int, void *sandboxInfo, cef_version_info_t *) {
  CefMainArgs args(instance);
  CefRefPtr<alkaid::HostApp> app = new alkaid::HostApp();
  const int subprocessExit = CefExecuteProcess(args, app, sandboxInfo);
  if (subprocessExit >= 0) return subprocessExit;
  SetCurrentProcessExplicitAppUserModelID(L"Alkaid.Desktop");
  if (!sandboxInfo) {
    MessageBoxW(nullptr, L"缺少沙箱启动信息，请通过 alkaid_host.exe 启动。", L"瑶光台", MB_OK | MB_ICONERROR);
    return 1;
  }
  std::wstring modulePath(32768, L'\0');
  const auto count = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
  if (!count || count >= modulePath.size()) return 2;
  modulePath.resize(count);
  const auto root = std::filesystem::path(modulePath).parent_path();
  const auto command = CefCommandLine::CreateCommandLine();
  command->InitFromString(GetCommandLineW());
  auto dataRoot = root / "data-v3";
  if (command->HasSwitch("alkaid-debug") && command->HasSwitch("alkaid-data-dir")) {
    dataRoot = std::filesystem::path(command->GetSwitchValue("alkaid-data-dir").ToWString());
  } else {
    const auto location = root / "data-location.txt";
    std::error_code configError;
    const bool configured = std::filesystem::exists(location, configError);
    if (configured || configError) {
      std::ifstream file(location, std::ios::binary | std::ios::ate);
      const auto size = file ? file.tellg() : std::streampos(-1);
      std::string value;
      if (size > 0 && size <= 8192) {
        value.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(value.data(), static_cast<std::streamsize>(value.size()))) value.clear();
      }
      if (value.starts_with("\xEF\xBB\xBF")) value.erase(0, 3);
      const auto length = value.find_first_of("\r\n");
      if (length != std::string::npos) value.resize(length);
      const auto chars = value.find('\0') == std::string::npos ?
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) : 0;
      std::wstring path(chars, L'\0');
      if (chars) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), path.data(), chars);
      dataRoot = std::filesystem::path(path);
    }
  }
  if (!dataRoot.is_absolute()) {
    MessageBoxW(nullptr, L"数据位置无效，请检查安装目录中的 data-location.txt。", L"瑶光台", MB_OK | MB_ICONERROR);
    return 3;
  }
  std::error_code error;
  std::filesystem::create_directories(dataRoot / "profiles" / "desktop", error);
  if (error) {
    MessageBoxW(nullptr, L"无法创建运行数据目录，请检查所选数据位置的写入权限。", L"瑶光台", MB_OK | MB_ICONERROR);
    return 3;
  }
  app->setRoot(root);
  app->setDataRoot(dataRoot);
  CefSettings settings;
  CefString(&settings.locale) = "zh-CN";
  // Chromium 要求每个持久化 profile 是 root_cache_path 的直接子目录。
  CefString(&settings.root_cache_path) = (dataRoot / "profiles").wstring();
  CefString(&settings.log_file) = (dataRoot / "cef.log").wstring();
  settings.log_severity = LOGSEVERITY_WARNING;
  settings.no_sandbox = false;
  // 调试端口必须显式开启，默认交付启动没有远程调试服务。
  if (command->HasSwitch("alkaid-debug")) settings.remote_debugging_port = 9227;
  if (!CefInitialize(args, settings, app, sandboxInfo)) return CefGetExitCode();
  CefRunMessageLoop();
  app->releaseContext();
  CefShutdown();
  return 0;
}
