#include "dialog_handler.h"

namespace alkaid {
// 禁用 <input type="file">
bool DialogHandler::OnFileDialog(
    CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString &title,
    const CefString &default_file_path,
    const std::vector<CefString> &accept_filters,
    const std::vector<CefString> &accept_extensions,
    const std::vector<CefString> &accept_descriptions,
    CefRefPtr<CefFileDialogCallback> callback) {
  callback->Cancel(); // 直接取消，不显示对话框
  return true;        // 表示已处理
}
} // namespace alkaid
