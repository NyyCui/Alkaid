/** @file url_policy.h 使用 CEF URL 解析器匹配宿主为应用分配的来源。 */
#ifndef ALKAID_URL_POLICY_H
#define ALKAID_URL_POLICY_H
#include "include/cef_parser.h"
#include <string>

namespace alkaid::UrlPolicy {
inline constexpr char DESKTOP_ORIGIN[] = "alkaid://desktop";
inline constexpr char DESKTOP_URL[] = "alkaid://desktop/index.html";
inline constexpr char DESKTOP_APP_ID[] = "org.alkaid.desktop";

/** 拒绝用户信息、端口和其他来源；仅返回规范来源，不使用字符串前缀匹配。 */
inline bool isApp(const CefString &url, const std::string &originKey) {
  CefURLParts parts;
  return CefParseURL(url, parts) && CefString(&parts.scheme) == "alkaid" &&
    CefString(&parts.host) == originKey && CefString(&parts.username).empty() &&
    CefString(&parts.password).empty() && CefString(&parts.port).empty();
}
inline bool isDesktop(const CefString &url) { return isApp(url, "desktop"); }
} // namespace alkaid::UrlPolicy
#endif // ALKAID_URL_POLICY_H
