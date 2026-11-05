/** @file resource_service.cpp 固定资源清单、真实状态码与内容安全策略。 */
#include "resource_service.h"
#include "url_policy.h"
#include "include/cef_resource_handler.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace alkaid {
namespace _ {
class AssetResponse : public CefResourceHandler {
  IMPLEMENT_REFCOUNTING(AssetResponse);
public:
  AssetResponse(std::shared_ptr<const WebAssets> assets, std::string path, std::string method)
      : assets_(std::move(assets)), path_(std::move(path)), method_(std::move(method)) {}
  bool Open(CefRefPtr<CefRequest>, bool &handleRequest, CefRefPtr<CefCallback>) override {
    handleRequest = true;
    const auto found = assets_->find(path_);
    if (method_ != "GET" && method_ != "HEAD") status_ = 405;
    else if (found != assets_->end()) { status_ = 200; asset_ = &found->second; }
    return true;
  }
  void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &length, CefString &) override {
    response->SetStatus(status_);
    response->SetStatusText(status_ == 200 ? "OK" : status_ == 404 ? "Not Found" : "Method Not Allowed");
    response->SetMimeType(asset_ ? asset_->mime : "text/plain");
    CefResponse::HeaderMap headers;
    headers.emplace("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; object-src 'none'; base-uri 'none'; frame-src 'none'; worker-src 'none'; form-action 'none'; frame-ancestors 'none'");
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("Cache-Control", "no-store");
    headers.emplace("Content-Length", std::to_string(asset_ ? asset_->bytes->size() : 0));
    response->SetHeaderMap(headers);
    length = method_ == "HEAD" ? 0 : static_cast<int64_t>(asset_ ? asset_->bytes->size() : 0);
  }
  bool Read(void *output, int size, int &read, CefRefPtr<CefResourceReadCallback>) override {
    read = 0;
    if (!asset_ || method_ == "HEAD" || size <= 0) return false;
    const auto amount = std::min(static_cast<std::size_t>(size), asset_->bytes->size() - offset_);
    if (!amount) return false;
    std::memcpy(output, asset_->bytes->data() + offset_, amount);
    offset_ += amount;
    read = static_cast<int>(amount);
    return true;
  }
  void Cancel() override { asset_ = nullptr; }
private:
  std::shared_ptr<const WebAssets> assets_;
  std::string path_;
  std::string method_;
  const WebAsset *asset_ = nullptr;
  std::size_t offset_ = 0;
  int status_ = 404;
};
} // namespace _

ResourceService::ResourceService(std::shared_ptr<const WebAssets> assets, std::string originKey)
    : assets_(std::move(assets)), originKey_(std::move(originKey)) {}

CefRefPtr<CefResourceHandler> ResourceService::Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &,
                                                     CefRefPtr<CefRequest> request) {
  if (!UrlPolicy::isApp(request->GetURL(), originKey_)) return nullptr;
  CefURLParts parts;
  if (!CefParseURL(request->GetURL(), parts)) return nullptr;
  auto path = CefString(&parts.path).ToString();
  if (path.empty() || path == "/") path = "/index.html";
  return new _::AssetResponse(assets_, path, request->GetMethod().ToString());
}

std::shared_ptr<const WebAssets> ResourceService::load(const std::filesystem::path &root, std::string &error,
    bool desktop, const std::string &stylesheet) {
  auto assets = std::make_shared<WebAssets>();
  std::map<std::string, std::string> manifest = {
    {"index.html", "text/html"},
    {"src/main.js", "text/javascript"}, {"src/bridge.js", "text/javascript"},
    {"sdk/index.js", "text/javascript"}, {"sdk/method_catalog.js", "text/javascript"},
    {"sdk/file_client.js", "text/javascript"}, {"sdk/errors.js", "text/javascript"}, {"sdk/network_client.js", "text/javascript"}
  };
  if (stylesheet.empty()) {
    manifest.emplace("1.css", "text/css");
    manifest.emplace("runtime.css", "text/css");
  } else manifest.emplace(stylesheet, "text/css");
  if (desktop) {
    manifest.emplace("src/packages.js", "text/javascript");
    manifest.emplace("src/interactions.js", "text/javascript");
    manifest.emplace("src/workspace.js", "text/javascript");
    manifest.emplace("experience.css", "text/css");
    manifest.emplace("assets/alkaid-256.png", "image/png");
  }
  for (const auto &[name, mime] : manifest) {
    std::ifstream input(root / name, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 4 * 1024 * 1024) {
      error = "桌面资源缺失或大小不合法：" + name;
      return nullptr;
    }
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0);
    std::string bytes(size, '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) {
      error = "桌面资源读取失败：" + name;
      return nullptr;
    }
    assets->emplace("/" + name, WebAsset{std::make_shared<const std::string>(std::move(bytes)), mime});
  }
  return assets;
}
std::shared_ptr<const WebAssets> ResourceService::fromPackage(std::shared_ptr<const VerifiedPackage> package) {
  auto assets = std::make_shared<WebAssets>();
  for (const auto &[name, bytes] : package->assets) {
    auto mime = CefGetMimeType(name.substr(name.rfind('.') + 1)).ToString();
    if (name.ends_with(".mjs")) mime = "text/javascript";
    if (mime.empty()) mime = "application/octet-stream";
    // 别名 shared_ptr 共享已验证包的所有权，UI 线程不复制大型资源内容。
    assets->emplace("/" + name, WebAsset{std::shared_ptr<const std::string>(package, &bytes), std::move(mime)});
  }
  return assets;
}
} // namespace alkaid
