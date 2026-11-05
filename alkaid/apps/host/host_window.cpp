/** @file host_window.cpp 原生窗口显示与关闭，最终退出由浏览器生命周期管理。 */
#include "host_window.h"
#include "app_runtime.h"
#include "include/views/cef_window.h"
#include "include/views/cef_display.h"
#include <algorithm>
#include <fstream>
#include "include/cef_image.h"

namespace alkaid {
void HostWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->SetTitle(title_);
  std::ifstream input(iconRoot_ / "alkaid-256.png", std::ios::binary);
  const std::string png((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto icon = CefImage::CreateImage();
  if (!png.empty() && icon->AddPNG(1, png.data(), png.size())) {
    window->SetWindowIcon(icon);
    window->SetWindowAppIcon(icon);
  }
  if (desktop_) tray_ = std::make_unique<TrayIcon>(window, iconRoot_ / "alkaid.ico");
  window->AddChildView(view_);
  auto display = savedBounds_ ? CefDisplay::GetDisplayMatchingBounds(*savedBounds_, false) : CefDisplay::GetPrimaryDisplay();
  if (!display) display = CefDisplay::GetPrimaryDisplay();
  const auto area = display ? display->GetWorkArea() : CefRect();
  if (area.width > 0 && area.height > 0) {
    minimumSize_ = CefSize(std::min(options_.minWidth, area.width), std::min(options_.minHeight, area.height));
    const bool restoreSize = savedBounds_.has_value() && options_.resizable;
    const auto width = std::clamp(restoreSize ? savedBounds_->width : options_.width, minimumSize_.width, area.width);
    const auto height = std::clamp(restoreSize ? savedBounds_->height : options_.height, minimumSize_.height, area.height);
    const bool visible = savedBounds_ && savedBounds_->x < area.x + area.width && savedBounds_->y < area.y + area.height &&
      savedBounds_->x + savedBounds_->width > area.x && savedBounds_->y + savedBounds_->height > area.y;
    const auto x = visible ? std::clamp(savedBounds_->x, area.x, area.x + area.width - width) : area.x + (area.width - width) / 2;
    const auto y = visible ? std::clamp(savedBounds_->y, area.y, area.y + area.height - height) : area.y + (area.height - height) / 2;
    window->SetBounds(CefRect(x, y, width, height));
  } else window->CenterWindow(GetPreferredSize(window));
  window->Show();
  shown_ = true;
  OnWindowBoundsChanged(window, window->GetBoundsInScreen());
  view_->RequestFocus();
}
CefSize HostWindow::GetMinimumSize(CefRefPtr<CefView>) { return minimumSize_; }
void HostWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect &bounds) {
  if (shown_ && !window->IsMaximized() && !window->IsMinimized() && !window->IsFullscreen())
    runtime_->rememberWindowBounds(appId_, bounds);
}
void HostWindow::OnWindowClosing(CefRefPtr<CefWindow>) {
  shown_ = false;
  runtime_->flushWindowBounds(appId_);
}
void HostWindow::OnWindowDestroyed(CefRefPtr<CefWindow>) {
  runtime_->flushWindowBounds(appId_);
  tray_.reset();
  view_ = nullptr;
}
bool HostWindow::CanClose(CefRefPtr<CefWindow>) {
  const auto browser = view_ ? view_->GetBrowser() : nullptr;
  if (!browser) return true;
  // CEF 已开始销毁的窗口必须完成关闭，否则会停在不可交互的半关闭状态。
  // 正常原生关闭在浏览器销毁之前进入本回调，先由平台执行保存协商。
  if (browser->GetHost()->IsReadyToBeClosed()) return true;
  return runtime_->requestWindowClose(appId_) && browser->GetHost()->TryCloseBrowser();
}
} // namespace alkaid
