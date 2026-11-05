#include "other_app.h"

namespace alkaid {
OtherApp::OtherApp() {
}
void OtherApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(RESOURCES_SCHEME, CEF_SCHEME_OPTION_STANDARD);
}
} // namespace alkaid
