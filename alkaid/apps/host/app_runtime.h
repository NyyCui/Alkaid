/** @file app_runtime.h 宿主拥有的应用目录、独立上下文与单实例窗口管理。 */
#ifndef ALKAID_APP_RUNTIME_H
#define ALKAID_APP_RUNTIME_H
#include "resource_service.h"
#include "runtime/runtime_types.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_thread.h"
#include "include/views/cef_overlay_controller.h"
#include "include/internal/cef_types_wrappers.h"
#include <map>
#include <atomic>

namespace alkaid {
class PackageManager;
class DataStore;
struct AppDefinition {
  std::string appId;
  std::string originKey;
  std::string name;
  std::string version;
  bool desktop = false;
  bool installed = false;
  std::set<std::string> permissions;
  std::set<std::string> grants;
  Json networkEndpoints = Json::array();
  Json declaredNetworkEndpoints = Json::array();
  std::set<std::string> pluginDependencies;
  std::map<std::string, PluginBinding> pluginBindings;
  std::string entry = "index.html";
  WindowOptions window;
  std::string publisherName, publisherKeyId, icon;
};

/** 仅在 CEF UI 线程访问；由 HostApp 持有直到消息循环结束。 */
class AppRuntime {
public:
  AppRuntime(std::filesystem::path root, std::filesystem::path dataRoot);
  ~AppRuntime();
  bool initialize(std::string &error);
  CefRefPtr<CefListValue> list() const;
  RuntimeResult<CefRefPtr<CefDictionaryValue>> act(const std::string &method, const std::string &appId);
  void contextReady(const std::string &appId, CefRefPtr<CefRequestContext> context);
  void browserCreated(const std::string &appId, CefRefPtr<CefBrowser> browser);
  void browserClosed(const std::string &appId);
  void shutdown();
  bool requestWindowClose(const std::string &appId);
  RuntimeResult<Json> windowCall(const std::string &appId, const std::string &method, const Json &params);
  RuntimeResult<Json> layoutWorkspace(const Json &params);
  void resetCloseHandler(const std::string &appId);
  void rememberWindowBounds(const std::string &appId, const CefRect &bounds);
  void flushWindowBounds(const std::string &appId);
  RuntimeResult<Json> packageCall(const std::string &method, const Json &params, CefRefPtr<CefBrowser> browser);
  void applyPackages(const PackageSnapshot &snapshot);
  bool canModifyPackage(const std::string &appId) const;
  bool pluginInUse(const std::string &pluginId) const;
  bool isInstalledPackage(const std::string &appId) const;
  std::string packageOriginKey(const std::string &appId) const;
  const std::filesystem::path &dataRoot() const { return dataRoot_; }
  void packageWorkFinished() { maybeQuit(); }
private:
  struct Entry {
    AppDefinition definition;
    std::shared_ptr<const WebAssets> assets;
    CefRefPtr<CefRequestContext> context;
    CefRefPtr<CefBrowser> browser;
    std::string state = "stopped";
    bool canRollback = false;
    std::string loadError;
    bool closeHandler = false, closeReady = false, closeApproved = false, closeParticipant = false;
    std::string closeTicket, closeMessage;
    std::optional<CefRect> bounds;
    bool boundsLoaded = false, boundsDirty = false, boundsTimer = false;
    CefRefPtr<CefOverlayController> overlay;
  };
  CefRefPtr<CefDictionaryValue> describe(const Entry &entry) const;
  std::optional<RuntimeError> start(Entry &entry);
  void focus(Entry &entry);
  void hideWorkspace(Entry &entry);
  void maybeQuit();
  void requestClose(Entry &entry, const std::string &reason);
  void cancelClose(Entry &entry, const std::string &message);
  void cancelShutdown(const std::string &message);
  void finishShutdown();
  void finishClose(Entry &entry);
  void emit(Entry &entry, const std::string &event, const Json &payload);
  std::filesystem::path root_;
  std::filesystem::path dataRoot_;
  std::set<std::string> browserCleanupFailures_;
  std::unique_ptr<PackageManager> packages_;
  std::shared_ptr<DataStore> windowStore_;
  CefRefPtr<CefThread> windowWorker_;
  std::map<std::string, Entry> entries_;
  std::string workspaceApp_;
  bool shuttingDown_ = false;
  bool exitPending_ = false;
  std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
};
} // namespace alkaid
#endif // ALKAID_APP_RUNTIME_H
