/** @file package_manager.h 原生选包、安装确认与后台任务，页面不传入本机路径。 */
#ifndef ALKAID_PACKAGE_MANAGER_H
#define ALKAID_PACKAGE_MANAGER_H
#include "package_store.h"
#include "include/cef_thread.h"
#include "include/cef_browser.h"
#include <atomic>

namespace alkaid {
class AppRuntime;
class PackageManager {
public:
  PackageManager(AppRuntime *runtime, std::filesystem::path root, std::filesystem::path dataRoot);
  ~PackageManager();
  void initialize();
  RuntimeResult<Json> call(const std::string &method, const Json &params, CefRefPtr<CefBrowser> browser);
  void selected(const std::string &taskId, const std::filesystem::path &path);
  void stop();
  bool working() const { return working_; }
  bool locks(const std::string &appId) const { return lockedApp_ == appId; }
  bool locksPlugin(const std::string &id) const { return lockedPlugin_ == id; }
private:
  struct PackSelection {
    std::string id, manifest;
    std::vector<std::filesystem::path> paths;
    bool localPublisher;
    bool interactive = false;
  };
  void pickPackFile(CefRefPtr<CefBrowser> browser, std::shared_ptr<PackSelection> job);
  void buildPackage(std::shared_ptr<PackSelection> job);
  struct Task { Json info; std::optional<PackagePreview> preview; std::shared_ptr<PackSelection> pack; };
  std::string createTask(const std::string &kind, const std::string &appId = "");
  void fail(const std::string &id, RuntimeError error);
  void mutate(const std::string &id, const std::string &kind, const std::string &appId,
    bool trust = false, Json permissions = nullptr, bool clearData = false);
  AppRuntime *runtime_;
  std::filesystem::path toolsRoot_, dataRoot_;
  std::shared_ptr<PackageStore> pluginStore_, store_;
  CefRefPtr<CefThread> worker_;
  std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
  std::map<std::string, Task> tasks_;
  std::vector<std::string> order_;
  bool ready_ = false;
  bool working_ = false;
  bool stopping_ = false;
  std::string lockedApp_;
  std::string lockedPlugin_;
  std::string activePreview_;
  std::optional<RuntimeError> startupError_;
  std::optional<RuntimeError> pluginError_;
  Json plugins_ = Json::array();
};
} // namespace alkaid
#endif // ALKAID_PACKAGE_MANAGER_H
