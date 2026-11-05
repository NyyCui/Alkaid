/** @file tray_icon.cpp 托盘显示和退出均复用桌面窗口生命周期。 */
#include "tray_icon.h"
namespace alkaid {
namespace { constexpr UINT TRAY_MESSAGE = WM_APP + 41; }
TrayIcon::TrayIcon(CefRefPtr<CefWindow> window, const std::filesystem::path &icon) : window_(window) {
  icon_ = static_cast<HICON>(LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
  WNDCLASSW type{};
  type.lpfnWndProc = handleMessage;
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"AlkaidTrayReceiver";
  RegisterClassW(&type);
  restart_ = RegisterWindowMessageW(L"TaskbarCreated");
  // 隐藏顶层窗口接收 Explorer 重启广播。
  receiver_ = CreateWindowExW(0, type.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, this);
  add();
}
TrayIcon::~TrayIcon() {
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = receiver_; data.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &data);
  if (receiver_) DestroyWindow(receiver_);
  if (icon_) DestroyIcon(icon_);
}
void TrayIcon::add() {
  if (!receiver_ || !icon_) return;
  NOTIFYICONDATAW data{sizeof(data)};
  data.hWnd = receiver_; data.uID = 1;
  data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data.uCallbackMessage = TRAY_MESSAGE; data.hIcon = icon_;
  wcscpy_s(data.szTip, L"瑶光台 · 双击显示桌面");
  Shell_NotifyIconW(NIM_ADD, &data);
}
void TrayIcon::show() {
  window_->Show();
  if (window_->IsMinimized()) window_->Restore();
  window_->Activate();
}
LRESULT CALLBACK TrayIcon::handleMessage(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
  }
  const auto self = reinterpret_cast<TrayIcon *>(GetWindowLongPtrW(handle, GWLP_USERDATA));
  if (self && message == self->restart_) self->add();
  if (self && message == TRAY_MESSAGE) {
    if (lparam == WM_LBUTTONDBLCLK) self->show();
    if (lparam == WM_RBUTTONUP) {
      const auto menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, 1, L"显示瑶光台");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, 2, L"退出瑶光台");
      POINT position{}; GetCursorPos(&position);
      SetForegroundWindow(handle);
      const auto action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, handle, nullptr);
      DestroyMenu(menu); PostMessageW(handle, WM_NULL, 0, 0);
      if (action == 1) self->show();
      if (action == 2) { self->show(); self->window_->Close(); }
    }
    return 0;
  }
  return DefWindowProcW(handle, message, wparam, lparam);
}
}
