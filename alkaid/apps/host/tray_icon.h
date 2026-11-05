/** @file tray_icon.h 桌面通知区域入口，窗口销毁时移除图标。 */
#ifndef ALKAID_TRAY_ICON_H
#define ALKAID_TRAY_ICON_H
#include "include/views/cef_window.h"
#include <filesystem>
#include <windows.h>
#include <shellapi.h>

namespace alkaid {
class TrayIcon {
public:
  TrayIcon(CefRefPtr<CefWindow> window, const std::filesystem::path &icon);
  ~TrayIcon();
  TrayIcon(const TrayIcon &) = delete;
  TrayIcon &operator=(const TrayIcon &) = delete;
private:
  static LRESULT CALLBACK handleMessage(HWND, UINT, WPARAM, LPARAM);
  void add();
  void show();
  CefRefPtr<CefWindow> window_;
  HWND receiver_ = nullptr;
  HICON icon_ = nullptr;
  UINT restart_ = 0;
};
}
#endif
