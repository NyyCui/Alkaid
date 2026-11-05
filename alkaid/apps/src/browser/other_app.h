#ifndef ALKAID_OTHER_APP_H
#define ALKAID_OTHER_APP_H
#include "include/cef_app.h"

namespace alkaid {

class OtherApp : public CefApp {
  IMPLEMENT_REFCOUNTING(OtherApp);

public:
  OtherApp();
  OtherApp(const OtherApp &) = delete;
  OtherApp &operator=(const OtherApp &) = delete;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
};

} // namespace alkaid

#endif // ALKAID_OTHER_APP_H
