/** @file resource_service.h 从预加载的只读资源提供页面，不接受 URL 指定磁盘路径。 */
#ifndef ALKAID_RESOURCE_SERVICE_H
#define ALKAID_RESOURCE_SERVICE_H
#include "include/cef_scheme.h"
#include "package_types.h"
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace alkaid {
struct WebAsset {
  std::shared_ptr<const std::string> bytes;
  std::string mime;
};
using WebAssets = std::map<std::string, WebAsset>;

class ResourceService : public CefSchemeHandlerFactory {
  IMPLEMENT_REFCOUNTING(ResourceService);
public:
  ResourceService(std::shared_ptr<const WebAssets> assets, std::string originKey);
  ResourceService(const ResourceService &) = delete;
  ResourceService &operator=(const ResourceService &) = delete;
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &,
                                       CefRefPtr<CefRequest> request) override;
  /** 只在 Browser 初始化阶段读取固定构建清单；错误通过 error 返回。 */
  static std::shared_ptr<const WebAssets> load(const std::filesystem::path &root, std::string &error,
      bool desktop, const std::string &stylesheet = {});
  static std::shared_ptr<const WebAssets> fromPackage(std::shared_ptr<const VerifiedPackage> package);
private:
  std::shared_ptr<const WebAssets> assets_;
  std::string originKey_;
};
} // namespace alkaid
#endif // ALKAID_RESOURCE_SERVICE_H
