/** @file host_client.h 将真实 CEF 文档身份连接到运行核心。 */
#ifndef ALKAID_HOST_CLIENT_H
#define ALKAID_HOST_CLIENT_H
#include "include/cef_client.h"
#include "include/cef_drag_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/wrapper/cef_message_router.h"
#include "rpc/call_gateway.h"
#include "app_runtime.h"
#include "data_store.h"
#include "network_store.h"
#include "include/cef_thread.h"
#include <map>

namespace alkaid {
class PluginProcess;
class HostClient : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDownloadHandler,
                   public CefResourceRequestHandler, public CefLoadHandler, public CefDragHandler,
                   public CefMessageRouterBrowserSide::Handler {
  IMPLEMENT_REFCOUNTING(HostClient);
public:
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  bool CanDownload(CefRefPtr<CefBrowser>,const CefString &,const CefString &) override { return false; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  void OnDraggableRegionsChanged(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const std::vector<CefDraggableRegion> &) override;
  HostClient(AppRuntime *runtime, AppDefinition definition);
  ~HostClient() override;
  HostClient(const HostClient &) = delete;
  HostClient &operator=(const HostClient &) = delete;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString &, const CefString &,
                     CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures &, CefWindowInfo &, CefRefPtr<CefClient> &,
                     CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &, bool *) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest>, bool, bool, const CefString &, bool &) override { return this; }
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefCallback>) override;
  void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, TransitionType) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int, const CefString &) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;
  bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t, const CefString &, bool,
               CefRefPtr<CefMessageRouterBrowserSide::Callback>) override;
  void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t queryId) override;
private:
  enum class Route { RuntimeInfo, Capabilities, Exit, AppList, AppAction, Package, Pack, PackageApp, PackageTask,
    PackageConfirm, WindowSet, WindowResolve, WindowControl, Data, WorkspaceFiles, Network, AppConfig, PlatformConfig, Subscribe, Unsubscribe, PluginCall, PluginTarget, PackagePermissions };
  std::optional<RuntimeError> changeSubscription(uint64_t session, Route route, const Json &params);
  std::optional<RuntimeError> eventAccess(uint64_t session, const std::string &event) const;
  void publish(CefRefPtr<CefBrowser> browser, uint64_t session, const std::string &event, Json payload);
  void registerMethods(Route route, std::initializer_list<const char *> names);
  void maintainStreams();
  void networkCall(int64_t queryId,const CallPermit &permit,const CallRequest &call,Json params,CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void pluginCall(int64_t queryId, const CallPermit &permit, const CallRequest &call, const PluginBinding &binding,
    Json params, CefRefPtr<CefBrowser> browser, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  struct DataRequest {
    CallPermit permit;
    std::shared_ptr<DataLease> lease;
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback;
    bool network = false;
  };
  void dataCall(int64_t queryId, const CallPermit &permit, const CallRequest &call, Json params,
                CefRefPtr<CefBrowser> browser, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
                std::shared_ptr<DataStore> store, std::string event);
  void dataComplete(int64_t queryId, RuntimeResult<Json> result, bool cleanupResources = true);
  void cancelData(int64_t queryId, bool reply);
  void invalidate(CefRefPtr<CefBrowser> browser);
  DocumentIdentity document(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) const;
  AppRuntime *runtime_;
  const AppDefinition definition_;
  SessionRegistry sessions_;
  CallGateway gateway_{sessions_};
  std::map<std::string, Route> routes_;
  std::map<std::pair<uint64_t, std::string>, std::string> subscriptions_;
  std::map<int, uint64_t> browserSessions_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  int renderRecoveryAttempts_ = 0;
  std::shared_ptr<DataStore> data_;
  std::shared_ptr<DataStore> appConfig_, platformConfig_;
  CefRefPtr<CefThread> dataWorker_;
  std::shared_ptr<NetworkStore> network_;
  CefRefPtr<CefThread> networkWorker_;
  std::shared_ptr<PluginProcess> plugins_;
  CefRefPtr<CefThread> pluginWorker_;
  std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
  std::map<int64_t, DataRequest> dataRequests_;
  bool choosingFile_ = false;
};
} // namespace alkaid
#endif // ALKAID_HOST_CLIENT_H
