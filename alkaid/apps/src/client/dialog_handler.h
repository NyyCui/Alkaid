#ifndef CEF_DIALOG_HANDLER_H
#define CEF_DIALOG_HANDLER_H
#include "include/cef_dialog_handler.h"

namespace alkaid {
class DialogHandler : public CefDialogHandler {
  IMPLEMENT_REFCOUNTING(DialogHandler);

public:
  bool OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                    const CefString &title, const CefString &default_file_path,
                    const std::vector<CefString> &accept_filters,
                    const std::vector<CefString> &accept_extensions,
                    const std::vector<CefString> &accept_descriptions,
                    CefRefPtr<CefFileDialogCallback> callback) override;
};
} // namespace alkaid
#endif // CEF_DIALOG_HANDLER_H
