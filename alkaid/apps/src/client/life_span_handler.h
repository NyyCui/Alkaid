#ifndef CEF_PROJECT_LIFE_SPAN_HANDLER_H
#define CEF_PROJECT_LIFE_SPAN_HANDLER_H
#include "include/cef_life_span_handler.h"

namespace alkaid {
class LifeSpanHandler : public CefLifeSpanHandler {
  IMPLEMENT_REFCOUNTING(LifeSpanHandler);
  LifeSpanHandler(const LifeSpanHandler &) = delete;

  LifeSpanHandler &operator=(const LifeSpanHandler &) = delete;
  using BrowserMap = std::map<int, CefRefPtr<CefBrowser>>;
  BrowserMap browser_map_;

public:
  LifeSpanHandler();

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     int popup_id, const CefString &target_url,
                     const CefString &target_frame_name,
                     WindowOpenDisposition target_disposition,
                     bool user_gesture, const CefPopupFeatures &popupFeatures,
                     CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client,
                     CefBrowserSettings &settings,
                     CefRefPtr<CefDictionaryValue> &extra_info,
                     bool *no_javascript_access) override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
};
} // namespace alkaid
#endif // CEF_PROJECT_LIFE_SPAN_HANDLER_H
