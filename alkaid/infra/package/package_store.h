/** @file package_store.h SQLite 激活注册表、发布者绑定和不可变包版本。 */
#ifndef ALKAID_PACKAGE_STORE_H
#define ALKAID_PACKAGE_STORE_H
#include "package_verifier.h"
#include <functional>
#include <windows.h>
struct sqlite3;

namespace alkaid {
/** 仅由单个工作线程使用；故障钩子只用于独立测试程序。 */
class PackageStore {
public:
  enum class Kind { Application, Plugin };
  explicit PackageStore(std::filesystem::path root, std::function<void(const std::string &)> fault = {},
                        Kind kind = Kind::Application, std::shared_ptr<const PackageStore> plugins = {})
    : root_(std::move(root)), kind_(kind), fault_(std::move(fault)), plugins_(std::move(plugins)) {}
  ~PackageStore();
  PackageStore(const PackageStore &) = delete;
  PackageStore &operator=(const PackageStore &) = delete;
  std::optional<RuntimeError> initialize();
  RuntimeResult<PackageSnapshot> load(bool recover = false);
  RuntimeResult<PackagePreview> preview(std::shared_ptr<const VerifiedPackage> package);
  std::optional<RuntimeError> install(const PackagePreview &preview, const std::string &operationId, bool trustPublisher);
  std::optional<RuntimeError> uninstall(const std::string &appId, const std::string &operationId);
  std::optional<RuntimeError> rollback(const std::string &appId, const std::string &operationId);
  std::optional<RuntimeError> setPermissions(const std::string &appId, const std::string &operationId, const Json &permissions);
  RuntimeResult<std::shared_ptr<const VerifiedPackage>> resolvePlugin(
    const std::string &id, const std::string &minimum, const std::string &maximum) const;
  Json history() const;
private:
  std::optional<RuntimeError> resolveDependencies(const VerifiedPackage &package,
    std::map<std::string, PluginBinding> &bindings) const;
  RuntimeResult<std::shared_ptr<const VerifiedPackage>> loadVersion(const std::string &digest, const std::string &appId, const std::string &key) const;
  void clean();
  std::filesystem::path root_;
  const Kind kind_;
  sqlite3 *db_ = nullptr;
  bool initialized_ = false;
  HANDLE lock_ = INVALID_HANDLE_VALUE;
  std::function<void(const std::string &)> fault_;
  std::shared_ptr<const PackageStore> plugins_;
};
} // namespace alkaid
#endif // ALKAID_PACKAGE_STORE_H
