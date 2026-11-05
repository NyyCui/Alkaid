/** @file host_client.cpp 每应用会话绑定、消息验证与受控生命周期调用。 */
#include "host_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "url_policy.h"
#include "capability_policy.h"
#include "function_task.h"
#include "plugin_process.h"
#include "runtime_audit.h"
#include "workspace_files.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace alkaid {
namespace _ {
class TextPicker : public CefRunFileDialogCallback {
  IMPLEMENT_REFCOUNTING(TextPicker);
public:
  explicit TextPicker(std::function<void(const std::vector<CefString> &)> done) : done_(std::move(done)) {}
  void OnFileDialogDismissed(const std::vector<CefString> &paths) override { done_(paths); }
private:
  std::function<void(const std::vector<CefString> &)> done_;
};

void reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback, const std::string &id,
           CefRefPtr<CefValue> result, const std::optional<RuntimeError> &error = std::nullopt) {
  auto envelope = CefDictionaryValue::Create();
  envelope->SetString("requestId", id);
  envelope->SetBool("ok", !error);
  if (error) {
    auto detail = CefDictionaryValue::Create();
    detail->SetString("code", error->code);
    detail->SetString("message", error->message);
    detail->SetBool("retryable", false);
    envelope->SetDictionary("error", detail);
  } else envelope->SetValue("result", result);
  auto value = CefValue::Create();
  value->SetDictionary(envelope);
  callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
}
} // namespace _
DocumentIdentity HostClient::document(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) const {
  return {browser->GetIdentifier(), frame->GetIdentifier().ToString(), frame->IsMain(),
          UrlPolicy::isApp(frame->GetURL(), definition_.originKey) ? "alkaid://" + definition_.originKey : ""};
}

HostClient::HostClient(AppRuntime *runtime, AppDefinition definition) : runtime_(runtime), definition_(std::move(definition)) {
  CefMessageRouterConfig config;
  config.js_query_function = "__alkaidQuery";
  config.js_cancel_function = "__alkaidCancel";
  router_ = CefMessageRouterBrowserSide::Create(config);
  router_->AddHandler(this, false);
  const auto declared = definition_.desktop ? std::set<std::string>{"apps.manage", "platform.exit"} : definition_.permissions;
  auto policy = definition_.desktop ? declared : CapabilityPolicy::allowed;
  if (definition_.appId == WorkspaceFiles::APP_ID) policy.insert("workspace.files");
  for (const auto &plugin : definition_.pluginDependencies) policy.insert("plugin." + plugin);
  const auto grants = definition_.desktop ? declared : definition_.grants;
  sessions_.registerApp({definition_.appId, definition_.originKey, declared}, {policy, grants, definition_.desktop});
  data_ = std::make_shared<DataStore>(runtime_->dataRoot() / "private" / definition_.originKey);
  const auto configRoot = runtime_->dataRoot() / "config";
  appConfig_ = std::make_shared<DataStore>(configRoot / "apps" / definition_.originKey, true);
  if (definition_.desktop) platformConfig_ = std::make_shared<DataStore>(configRoot / "platform", true);
  dataWorker_ = CefThread::CreateThread("alkaid-data");
  network_=std::make_shared<NetworkStore>(definition_.networkEndpoints);
  networkWorker_=CefThread::CreateThread("alkaid-network");
  if (!definition_.pluginBindings.empty()) {
    plugins_ = std::make_shared<PluginProcess>(runtime_->dataRoot(), definition_.appId);
    pluginWorker_ = CefThread::CreateThread("alkaid-plugins");
  }
  registerMethods(Route::RuntimeInfo, {"runtime.getInfo"});
  registerMethods(Route::Capabilities, {"runtime.getCapabilities"});
  registerMethods(Route::Exit, {"runtime.exit"});
  registerMethods(Route::AppList, {"apps.list"});
  registerMethods(Route::AppAction, {"apps.launch", "apps.focus", "apps.close"});
  registerMethods(Route::Package, {"apps.prepareInstall", "apps.listTasks", "apps.listAudit", "plugins.list", "plugins.prepareInstall"});
  registerMethods(Route::PluginTarget, {"plugins.uninstall"});
  registerMethods(Route::Pack, {"apps.preparePack", "apps.localPublisher", "apps.publisherBackup"});
  registerMethods(Route::PackageApp, {"apps.uninstall", "apps.rollback", "apps.clearData", "apps.clearBrowserData"});
  registerMethods(Route::PackagePermissions, {"apps.setPermissions"});
  registerMethods(Route::PackageTask, {"apps.getTask", "apps.cancelInstall", "apps.advancePack", "apps.cancelPack", "apps.revealPack", "apps.installPacked"});
  registerMethods(Route::PackageConfirm, {"apps.confirmInstall"});
  registerMethods(Route::WindowControl, {"window.minimize", "window.toggleMaximize", "apps.layoutWorkspace"});
  registerMethods(Route::WindowSet, {"window.setCloseHandler"});
  registerMethods(Route::WindowResolve, {"window.resolveClose"});
  registerMethods(Route::Subscribe, {"events.subscribe"});
  registerMethods(Route::Unsubscribe, {"events.unsubscribe"});
  registerMethods(Route::PluginCall, {"plugin.call"});
  if (dataWorker_) registerMethods(Route::WorkspaceFiles, {"workspace.files"});
  if (dataWorker_) registerMethods(Route::Data, {"storage.get", "storage.set", "storage.remove", "storage.batch",
    "files.readPrivateText", "files.writePrivateText", "files.removePrivate", "dialog.openFile", "dialog.saveFile",
    "files.readText", "files.writeText", "files.close", "files.beginRead", "files.beginWrite", "files.beginPrivateRead",
    "files.beginPrivateWrite", "files.readNext", "files.appendChunk", "files.commitWrite", "files.abortStream"});
  if (networkWorker_) registerMethods(Route::Network, {"net.getEndpoints", "net.open", "net.readNext", "net.close"});
  if (dataWorker_) {
    registerMethods(Route::AppConfig, {"config.app.get", "config.app.set", "config.app.remove", "config.app.batch"});
    registerMethods(Route::PlatformConfig, {"config.platform.get", "config.platform.set", "config.platform.remove", "config.platform.batch"});
  }
  maintainStreams();
}

void HostClient::registerMethods(Route route, std::initializer_list<const char *> names) {
  for (const auto name : names) {
    if (!gateway_.enableMethod(name)) routes_.emplace(name, route);
  }
}

std::optional<RuntimeError> HostClient::changeSubscription(uint64_t session, Route route, const Json &params) {
  const auto id = params["subscriptionId"].get<std::string>();
  if (!std::ranges::all_of(id, [](unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
  })) return RuntimeError{"INVALID_REQUEST", "订阅标识不合法"};
  const auto key = std::make_pair(session, id);
  if (route == Route::Unsubscribe) {
    subscriptions_.erase(key);
    return std::nullopt;
  }
  const auto event = params["event"].get<std::string>();
  if (const auto error = eventAccess(session, event)) return error;
  if (subscriptions_.contains(key)) return RuntimeError{"SUBSCRIPTION_EXISTS", "订阅标识正在使用"};
  if (subscriptions_.size() >= 32) return RuntimeError{"RESOURCE_EXHAUSTED", "当前窗口订阅数量已达上限"};
  subscriptions_.emplace(key, event);
  return std::nullopt;
}

std::optional<RuntimeError> HostClient::eventAccess(uint64_t session, const std::string &event) const {
  if (dataWorker_ && event == "config.app.changed") return std::nullopt;
  if (dataWorker_ && event == "config.platform.changed") {
    if (sessions_.allows(session, "apps.manage", true)) return std::nullopt;
    return RuntimeError{"PERMISSION_DENIED", "应用没有订阅平台配置事件的权限"};
  }
  const auto delimiter = event.rfind('.');
  if (event.starts_with("plugin.") && delimiter > 7) {
    const auto id = event.substr(7, delimiter - 7);
    if (!definition_.pluginDependencies.contains(id) || !sessions_.allows(session, "plugin." + id, false))
      return RuntimeError{"PERMISSION_DENIED", "应用没有订阅此插件事件的权限"};
    const auto binding = definition_.pluginBindings.find(id);
    if (!pluginWorker_ || binding == definition_.pluginBindings.end())
      return RuntimeError{"PLUGIN_UNAVAILABLE", "当前会话没有绑定插件版本"};
    if (binding->second.events.contains(event.substr(delimiter + 1))) return std::nullopt;
  }
  return RuntimeError{"EVENT_NOT_FOUND", "事件未注册"};
}

void HostClient::publish(CefRefPtr<CefBrowser> browser, uint64_t session, const std::string &event, Json payload) {
  const auto found = browserSessions_.find(browser->GetIdentifier());
  if (found == browserSessions_.end() || found->second != session || !sessions_.find(session) || !browser->IsValid()) return;
  if (eventAccess(session, event)) return;
  const auto frame = browser->GetMainFrame();
  if (!frame || !UrlPolicy::isApp(frame->GetURL(), definition_.originKey)) return;
  for (const auto &[key, subscribed] : subscriptions_) {
    if (key.first != session || subscribed != event) continue;
    payload["subscriptionId"] = key.second;
    const auto value = Json(Json{{"event", event}, {"payload", payload}}.dump()).dump(-1, ' ', true);
    frame->ExecuteJavaScript("globalThis.dispatchEvent(new CustomEvent('alkaid:runtime-event',{detail:JSON.parse(" + value + ")}));",
      frame->GetURL(), 0);
  }
}

HostClient::~HostClient() {
  alive_->store(false);
  for (auto &[id, request] : dataRequests_) request.lease->active.store(false);
  if (dataWorker_) dataWorker_->Stop();
  if (networkWorker_) networkWorker_->Stop();
  if (pluginWorker_) pluginWorker_->Stop();
  router_->RemoveHandler(this);
}
void HostClient::maintainStreams() {
  const auto alive=alive_;
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive] {
    if (!alive->load()) return;
    if (dataWorker_) dataWorker_->GetTaskRunner()->PostTask(new FunctionTask([data=data_] { data->expireStreams(); }));
    if (networkWorker_) networkWorker_->GetTaskRunner()->PostTask(new FunctionTask([network=network_] { network->expire(); }));
    maintainStreams();
  }),30000);
}

void HostClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (sessions_.bindBrowser(browser->GetIdentifier(), definition_.appId)) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  runtime_->browserCreated(definition_.appId, browser);
}

void HostClient::invalidate(CefRefPtr<CefBrowser> browser) {
  const auto found = browserSessions_.find(browser->GetIdentifier());
  if (found != browserSessions_.end()) {
    const auto session = found->second;
    std::erase_if(subscriptions_, [session](const auto &entry) { return entry.first.first == session; });
    std::vector<int64_t> cancelled;
    for (const auto &[id, request] : dataRequests_) if (request.permit.sessionId == session) cancelled.push_back(id);
    for (const auto id : cancelled) cancelData(id, false);
    if (dataWorker_) dataWorker_->GetTaskRunner()->PostTask(new FunctionTask([data = data_, session] { data->revoke(session); }));
    if (networkWorker_) networkWorker_->GetTaskRunner()->PostTask(new FunctionTask([network=network_,session] { network->revoke(session); }));
    if (pluginWorker_) pluginWorker_->GetTaskRunner()->PostTask(new FunctionTask([plugins=plugins_,session] { plugins->release(session); }));
    gateway_.discardSession(found->second);
    browserSessions_.erase(found);
  }
  sessions_.invalidateDocument(browser->GetIdentifier());
  runtime_->resetCloseHandler(definition_.appId);
}

void HostClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeClose(browser);
  invalidate(browser);
  sessions_.unbindBrowser(browser->GetIdentifier());
  runtime_->browserClosed(definition_.appId);
}

bool HostClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString &, const CefString &,
    CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures &, CefWindowInfo &, CefRefPtr<CefClient> &,
    CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &, bool *) { return true; }

bool HostClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request, bool, bool) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain() || !UrlPolicy::isApp(request->GetURL(), definition_.originKey)) return true;
  CefURLParts parts;
  CefParseURL(request->GetURL(), parts);
  const auto path = CefString(&parts.path).ToString();
  if (path != "/" && path != "/index.html") return true;
  invalidate(browser);
  router_->OnBeforeBrowse(browser, frame);
  return false;
}

CefResourceRequestHandler::ReturnValue HostClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) {
  // 页面只能加载自身资源；业务网络走独立原生代理，不能借资源请求绕过端点授权。
  return UrlPolicy::isApp(request->GetURL(), definition_.originKey) ? RV_CONTINUE : RV_CANCEL;
}

void HostClient::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) return;
  invalidate(browser);
  const auto session = sessions_.openDocument(document(browser, frame));
  if (const auto id = std::get_if<uint64_t>(&session)) browserSessions_[browser->GetIdentifier()] = *id;
}

void HostClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString &) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnRenderProcessTerminated(browser);
  invalidate(browser);
  // 宿主窗口仍可关闭；有限恢复当前应用，不自动重放任何系统调用。
  if (++renderRecoveryAttempts_ <= 2) browser->Reload();
}

bool HostClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                         CefProcessId source, CefRefPtr<CefProcessMessage> message) {
  if (source != PID_RENDERER || !frame->IsMain() || !UrlPolicy::isApp(frame->GetURL(), definition_.originKey)) return false;
  return router_->OnProcessMessageReceived(browser, frame, source, message);
}

bool HostClient::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t queryId,
                         const CefString &request, bool persistent,
                         CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  const auto raw = request.ToString();
  if (persistent || raw.size() > MAX_PAYLOAD_BYTES) {
    callback->Failure(1, "不支持持久请求或报文过大");
    return true;
  }
  const auto parsed = CefParseJSON(raw, JSON_PARSER_RFC);
  const auto object = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
  const std::string id = object && object->GetType("requestId") == VTYPE_STRING ? object->GetString("requestId").ToString() : "";
  if (!object || object->GetSize() != 5 || object->GetType("requestId") != VTYPE_STRING ||
      object->GetType("protocolVersion") != VTYPE_INT || object->GetType("method") != VTYPE_STRING ||
      object->GetType("timeoutMs") != VTYPE_INT || object->GetType("params") != VTYPE_DICTIONARY) {
    _::reply(callback, id, nullptr, RuntimeError{"INVALID_REQUEST", "请求信封或方法参数不合法"});
    return true;
  }
  const auto found = browserSessions_.find(browser->GetIdentifier());
  if (found == browserSessions_.end()) {
    _::reply(callback, id, nullptr, RuntimeError{"SESSION_EXPIRED", "页面会话尚未建立或已经失效"});
    return true;
  }
  const CallRequest call{object->GetInt("protocolVersion"), id, object->GetString("method").ToString(), raw.size(), object->GetInt("timeoutMs")};
  const auto handler = routes_.find(call.method);
  if (handler == routes_.end()) {
    _::reply(callback, id, nullptr, RuntimeError{"METHOD_NOT_FOUND", "系统能力尚未实现或未启用"});
    return true;
  }
  const auto route = handler->second;
  const auto params = object->GetDictionary("params");
  auto parameterValue = CefValue::Create();
  parameterValue->SetDictionary(params->Copy(false));
  const auto dataParams = Json::parse(CefWriteJSON(parameterValue,JSON_WRITER_DEFAULT).ToString(),nullptr,false);
  const bool configAction = route == Route::AppConfig || route == Route::PlatformConfig;
  const bool workspaceFiles = route == Route::WorkspaceFiles;
  const bool dataAction = route == Route::Data || configAction || workspaceFiles;
  const auto dataMethod = configAction ? "storage" + call.method.substr(call.method.rfind('.')) : call.method;
  const bool networkAction = route == Route::Network;
  const bool setClose = route == Route::WindowSet, resolveClose = route == Route::WindowResolve;
  const bool windowAction=setClose || resolveClose || route == Route::WindowControl;
  const bool appAction = route == Route::AppAction;
  const bool appTarget = appAction || route == Route::PackageApp;
  const bool hasUninstallOption = call.method == "apps.uninstall" && params->GetType("clearData") == VTYPE_BOOL;
  const bool taskTarget = route == Route::PackageTask;
  const bool confirm = route == Route::PackageConfirm;
  const bool pack = route == Route::Pack;
  const bool eventAction = route == Route::Subscribe || route == Route::Unsubscribe;
  const bool pluginAction = route == Route::PluginCall;
  const bool pluginTarget = route == Route::PluginTarget;
  const bool editPermissions = route == Route::PackagePermissions;
  const bool packageAction = route == Route::Package || route == Route::PackageApp || taskTarget || confirm || pack || pluginTarget || editPermissions;
  const auto validString = [&](const char *key, std::size_t maximum) {
    return params->GetType(key) == VTYPE_STRING && !params->GetString(key).empty() && params->GetString(key).ToString().size() <= maximum;
  };
  const bool workspaceLayout = call.method == "apps.layoutWorkspace";
  if ((workspaceLayout && (params->GetSize() != 9 || !validString("appId", 128) ||
        params->GetType("x") != VTYPE_INT || params->GetType("y") != VTYPE_INT ||
        params->GetType("width") != VTYPE_INT || params->GetType("height") != VTYPE_INT ||
        params->GetType("visible") != VTYPE_BOOL || params->GetType("focus") != VTYPE_BOOL ||
        params->GetType("persist") != VTYPE_BOOL || params->GetType("minimize") != VTYPE_BOOL)) ||
      (route == Route::WindowControl && !workspaceLayout && params->GetSize() != 0) ||
      (editPermissions && (params->GetSize() != 2 || !validString("appId", 128) ||
        !dataParams.contains("permissions") || !CapabilityPolicy::valid(dataParams["permissions"], true))) ||
      (pluginTarget && (params->GetSize() != 1 || !validString("pluginId", 100) ||
        !PackagePolicy::appId(dataParams["pluginId"].get<std::string>()))) ||
      (pluginAction && (!PackagePolicy::keys(dataParams, {"pluginId", "method", "params"}) ||
        !validString("pluginId", 100) || !PackagePolicy::appId(dataParams["pluginId"].get<std::string>()) ||
        !validString("method", 64) || !PackagePolicy::pluginMember(dataParams["method"].get<std::string>()) ||
        dataParams["params"].dump().size() > 240 * 1024)) ||
      (eventAction && (params->GetSize() != (route == Route::Subscribe ? 2 : 1) ||
        !validString("subscriptionId", 128) || (route == Route::Subscribe && !validString("event", 172)))) ||
      (call.method == "apps.localPublisher" && params->GetSize() != 0 &&
        (params->GetSize() != 1 || !validString("name", 120))) ||
      (call.method == "apps.publisherBackup" && (params->GetSize() != 2 || !validString("password", 256) ||
        params->GetString("password").length() < 12 || params->GetType("restore") != VTYPE_BOOL)) ||
      (call.method == "apps.preparePack" && (!validString("manifest",256 * 1024) ||
        (params->GetSize() != 1 && params->GetSize() != 2 && params->GetSize() != 3) ||
        (params->HasKey("localPublisher") && params->GetType("localPublisher") != VTYPE_BOOL) ||
        (params->HasKey("interactive") && params->GetType("interactive") != VTYPE_BOOL) ||
        dataParams.size() != 1 + dataParams.contains("localPublisher") + dataParams.contains("interactive"))) ||
      (appTarget && (params->GetSize() != (hasUninstallOption ? 2 : 1) || !validString("appId",128))) ||
      (taskTarget && (params->GetSize() != 1 || !validString("taskId",64))) ||
      (confirm && (params->GetSize() != 2 || !validString("taskId",64) || params->GetType("trustPublisher") != VTYPE_BOOL)) ||
      (setClose && (params->GetSize()!=1 || params->GetType("enabled")!=VTYPE_BOOL)) ||
      (resolveClose && (params->GetSize()!=2 || !validString("requestId",64) || params->GetType("allow")!=VTYPE_BOOL)) ||
      (dataAction && !workspaceFiles && !DataStore::validParams(dataMethod,dataParams)) ||
      (networkAction && !NetworkStore::validParams(call.method,dataParams)) ||
      (!editPermissions && !pluginTarget && !pluginAction && !eventAction && !pack && !appTarget && !taskTarget && !confirm && !dataAction && !windowAction && !networkAction && params->GetSize() != 0)) {
    _::reply(callback, id, nullptr, RuntimeError{"INVALID_REQUEST", "方法参数不合法"});
    return true;
  }
  const auto admitted = gateway_.begin(found->second, document(browser, frame), call);
  if (const auto error = std::get_if<RuntimeError>(&admitted)) {
    _::reply(callback, id, nullptr, *error);
    return true;
  }
  const auto permit = std::get<CallPermit>(admitted);
  auto result = CefValue::Create();
  result->SetNull();
  if (const auto error = gateway_.checkReady(permit)) {
    gateway_.finish(permit);
    _::reply(callback, id, nullptr, error);
    return true;
  }
  if (dataAction) {
    auto operation = call;
    operation.method = dataMethod;
    const auto &store = route == Route::AppConfig ? appConfig_ : route == Route::PlatformConfig ? platformConfig_ : data_;
    const auto event = !configAction || dataMethod == "storage.get" ? "" :
      route == Route::AppConfig ? "config.app.changed" : "config.platform.changed";
    dataCall(queryId,permit,operation,dataParams,browser,callback,store,event);
    return true;
  }
  if(networkAction) { networkCall(queryId,permit,call,dataParams,callback); return true; }
  if (pluginAction) {
    const auto pluginId = dataParams["pluginId"].get<std::string>();
    const auto authorized = definition_.pluginDependencies.contains(pluginId) &&
      sessions_.allows(permit.sessionId, "plugin." + pluginId, false);
    const auto binding = definition_.pluginBindings.find(pluginId);
    if (authorized && binding != definition_.pluginBindings.end() && pluginWorker_) {
      pluginCall(queryId, permit, call, binding->second, dataParams, browser, callback);
      return true;
    }
    const RuntimeError error = authorized
      ? RuntimeError{"PLUGIN_UNAVAILABLE", "当前会话没有可用的插件实例"}
      : RuntimeError{"PERMISSION_DENIED", "当前应用未声明或未获准调用此插件"};
    gateway_.finish(permit);
    _::reply(callback, id, nullptr, error);
    return true;
  }
  if (eventAction) {
    if (const auto error = changeSubscription(permit.sessionId, route, dataParams)) {
      gateway_.finish(permit);
      _::reply(callback, id, nullptr, error);
      return true;
    }
  } else if (windowAction) {
    const auto action=workspaceLayout ? runtime_->layoutWorkspace(dataParams) : runtime_->windowCall(definition_.appId,call.method,dataParams);
    if (const auto failure=std::get_if<RuntimeError>(&action)) { gateway_.finish(permit); _::reply(callback,id,nullptr,*failure); return true; }
  } else if (route == Route::RuntimeInfo) {
    auto info = CefDictionaryValue::Create();
    info->SetInt("protocolVersion", PROTOCOL_VERSION);
    info->SetString("runtimeVersion", "2.0.0-dev.6");
    info->SetString("appId", sessions_.find(found->second)->appId);
    result->SetDictionary(info);
  } else if (route == Route::Capabilities) {
    auto list = CefListValue::Create();
    const auto methods = gateway_.capabilities(found->second);
    for (std::size_t index = 0; index < methods.size(); ++index) list->SetString(index, methods[index]);
    result->SetList(list);
  } else if (route == Route::AppList) {
    result->SetList(runtime_->list());
  } else if (packageAction) {
    const auto action = runtime_->packageCall(call.method, dataParams, browser);
    if (const auto failure = std::get_if<RuntimeError>(&action)) {
      gateway_.finish(permit);
      _::reply(callback,id,nullptr,*failure);
      return true;
    }
    result = CefParseJSON(std::get<Json>(action).dump(),JSON_PARSER_RFC);
  } else if (appAction) {
    const auto action = runtime_->act(call.method, params->GetString("appId").ToString());
    if (const auto failure = std::get_if<RuntimeError>(&action)) {
      gateway_.finish(permit);
      _::reply(callback, id, nullptr, *failure);
      return true;
    }
    result->SetDictionary(std::get<CefRefPtr<CefDictionaryValue>>(action));
  }
  const auto error = gateway_.finish(permit);
  _::reply(callback, id, result, error);
  if (!error && route == Route::Exit) {
    runtime_->shutdown();
  }
  return true;
}
void HostClient::cancelData(int64_t queryId, bool respond) {
  const auto found = dataRequests_.find(queryId);
  if (found == dataRequests_.end()) return;
  const auto request = found->second;
  request.lease->active.store(false);
  gateway_.finish(request.permit);
  dataRequests_.erase(found);
  if (respond) _::reply(request.callback,request.permit.requestId,nullptr,RuntimeError{"TIMEOUT","系统调用超时，已经提交的写入无法撤销"});
}
void HostClient::OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t queryId) { cancelData(queryId,false); }
void HostClient::dataComplete(int64_t queryId, RuntimeResult<Json> result, bool cleanupResources) {
  CEF_REQUIRE_UI_THREAD();
  const auto found = dataRequests_.find(queryId);
  // 对话框结果与取消请求可能交错；迟到句柄立即关闭，不能泄漏到下一次调用。
  const auto cleanup = [&] {
    if (!cleanupResources) return;
    const auto value = std::get_if<Json>(&result);
    if (value && value->is_object() && value->contains("handle") && dataWorker_) {
      const auto handle = (*value)["handle"].get<std::string>();
      dataWorker_->GetTaskRunner()->PostTask(new FunctionTask([data = data_,handle] { data->release(handle); }));
    }
    if (value && value->is_object() && value->contains("streamId") && dataWorker_) {
      const auto stream=(*value)["streamId"].get<std::string>();
      dataWorker_->GetTaskRunner()->PostTask(new FunctionTask([data=data_,stream] { data->releaseTransfer(stream); }));
    }
    if(value && value->is_object() && value->contains("transferId") && networkWorker_) {
      const auto id=(*value)["transferId"].get<std::string>();
      networkWorker_->GetTaskRunner()->PostTask(new FunctionTask([network=network_,id] { network->release(id); }));
    }
  };
  if (found == dataRequests_.end()) { cleanup(); return; }
  const auto request = found->second;
  dataRequests_.erase(found);
  const auto error = gateway_.finish(request.permit);
  request.lease->active.store(false);
  if (error) { cleanup(); _::reply(request.callback,request.permit.requestId,nullptr,error); }
  else if (const auto failure = std::get_if<RuntimeError>(&result)) _::reply(request.callback,request.permit.requestId,nullptr,*failure);
  else _::reply(request.callback,request.permit.requestId,CefParseJSON(std::get<Json>(result).dump(),JSON_PARSER_RFC));
}
void HostClient::pluginCall(int64_t queryId, const CallPermit &permit, const CallRequest &call, const PluginBinding &binding,
    Json params, CefRefPtr<CefBrowser> browser, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (dataRequests_.size() >= 16) {
    gateway_.finish(permit);
    _::reply(callback, permit.requestId, nullptr, RuntimeError{"RESOURCE_EXHAUSTED", "当前窗口在途操作过多"});
    return;
  }
  auto lease = std::make_shared<DataLease>();
  lease->deadline = RuntimeClock::now() + std::chrono::milliseconds(call.timeoutMs);
  dataRequests_.emplace(queryId, DataRequest{permit,lease,callback});
  const auto alive = alive_;
  CefPostDelayedTask(TID_UI, new FunctionTask([this,alive,queryId] { if (alive->load()) cancelData(queryId,true); }),call.timeoutMs);
  const auto runner = pluginWorker_->GetTaskRunner();
  const bool posted = runner->PostTask(new FunctionTask([this,alive,queryId,lease,permit,binding,params,browser,plugins=plugins_,runner] {
    const auto id = params["pluginId"].get<std::string>();
    auto result = plugins->call(id, binding, params["method"].get<std::string>(), params["params"], permit, *lease,
      [this,alive,browser,session=permit.sessionId,id](std::string name, Json data) {
        CefPostTask(TID_UI, new FunctionTask([this,alive,browser,session,event="plugin." + id + "." + name,data=std::move(data)] {
          if (alive->load()) publish(browser,session,event,{{"data",data}});
        }));
      });
    plugins->startPolling(runner);
    if (const auto error = std::get_if<RuntimeError>(&result); error && error->code == "PLUGIN_UNAVAILABLE")
      RuntimeAudit::record(runtime_->dataRoot(), "pluginCall", definition_.appId, id, "PLUGIN_UNAVAILABLE");
    CefPostTask(TID_UI, new FunctionTask([this,alive,queryId,result=std::move(result)]() mutable {
      if (alive->load()) dataComplete(queryId,std::move(result),false);
    }));
  }));
  if (!posted) dataComplete(queryId,RuntimeError{"WORKER_UNAVAILABLE","插件工作线程不可用"});
}
void HostClient::networkCall(int64_t queryId,const CallPermit &permit,const CallRequest &call,Json params,CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  std::size_t pending=0;for(const auto &[id,request]:dataRequests_) if(request.network)++pending;
  if(!networkWorker_||pending>=4) { gateway_.finish(permit);_::reply(callback,permit.requestId,nullptr,RuntimeError{"RESOURCE_EXHAUSTED","网络服务繁忙，每应用最多 4 个在途请求"});return; }
  auto lease=std::make_shared<DataLease>();lease->deadline=RuntimeClock::now()+std::chrono::milliseconds(call.timeoutMs);
  dataRequests_.emplace(queryId,DataRequest{permit,lease,callback,true});const auto alive=alive_;
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive,queryId]{if(alive->load())cancelData(queryId,true);}),call.timeoutMs);
  const bool posted=networkWorker_->GetTaskRunner()->PostTask(new FunctionTask([this,alive,queryId,lease,permit,method=call.method,params,network=network_] {
    auto result=network->call(method,params,permit.sessionId,*lease);
    CefPostTask(TID_UI,new FunctionTask([this,alive,queryId,result=std::move(result)]() mutable {if(alive->load())dataComplete(queryId,std::move(result));}));
  }));
  if(!posted)dataComplete(queryId,RuntimeError{"WORKER_UNAVAILABLE","网络工作线程不可用"});
}
void HostClient::dataCall(int64_t queryId, const CallPermit &permit, const CallRequest &call, Json params,
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
    std::shared_ptr<DataStore> store, std::string event) {
  const bool dialog = call.method.starts_with("dialog.");
  if (!dataWorker_ || dataRequests_.size() >= 16 || (dialog && choosingFile_)) {
    gateway_.finish(permit);
    _::reply(callback,permit.requestId,nullptr,RuntimeError{"RESOURCE_EXHAUSTED","文件服务繁忙，请完成当前选择或稍后重试"}); return;
  }
  auto lease = std::make_shared<DataLease>();
  lease->deadline = RuntimeClock::now() + std::chrono::milliseconds(call.timeoutMs);
  dataRequests_.emplace(queryId,DataRequest{permit,lease,callback});
  const auto alive = alive_;
  CefPostDelayedTask(TID_UI,new FunctionTask([this,alive,queryId] { if (alive->load()) cancelData(queryId,true); }),call.timeoutMs);
  Json keys = Json::array();
  if (!event.empty()) {
    std::set<std::string> unique;
    if (call.method == "storage.batch") {
      for (const auto &item : params["operations"]) {
        const auto key = item["key"].get<std::string>();
        if (unique.insert(key).second) keys.push_back(key);
      }
    } else keys.push_back(params["key"]);
  }
  const auto enqueue = [this,alive,queryId,lease,permit,method = call.method,params,store,browser,event,keys](const std::filesystem::path &path) {
    if (!alive->load()) return;
    if (const auto error = gateway_.checkReady(permit)) { dataComplete(queryId,*error); return; }
    const bool posted = dataWorker_->GetTaskRunner()->PostTask(new FunctionTask([this,alive,queryId,lease,permit,method,params,path,store,browser,event,keys] {
      RuntimeResult<Json> result = method == "workspace.files" ? WorkspaceFiles::call(runtime_->dataRoot() / "workspace", params, *lease) :
        method.starts_with("dialog.") ? store->grant(path,method == "dialog.saveFile",permit.sessionId,*lease) :
        store->call(method,params,permit.sessionId,*lease);
      CefPostTask(TID_UI,new FunctionTask([this,alive,queryId,session=permit.sessionId,browser,event,keys,result = std::move(result)]() mutable {
        if (!alive->load()) return;
        const bool notify = !event.empty() && std::holds_alternative<Json>(result);
        dataComplete(queryId,std::move(result));
        if (notify) publish(browser,session,event,{{"keys",keys}});
      }));
    }));
    if (!posted) dataComplete(queryId,RuntimeError{"WORKER_UNAVAILABLE","文件工作线程不可用"});
  };
  if (!dialog) { enqueue({}); return; }
  choosingFile_ = true;
  const bool save = call.method == "dialog.saveFile";
  browser->GetHost()->RunFileDialog(save ? FILE_DIALOG_SAVE : FILE_DIALOG_OPEN,
    definition_.name + (save ? " · 保存文件" : " · 打开文件"), save ? "untitled.txt" : "", {"*"},
    new _::TextPicker([this,alive,queryId,enqueue](const std::vector<CefString> &paths) {
      if (!alive->load()) return;
      choosingFile_ = false;
      if (paths.empty()) dataComplete(queryId,Json(nullptr));
      else enqueue(std::filesystem::path(paths[0].ToWString()));
    }));
}
} // namespace alkaid

namespace alkaid {
void HostClient::OnDraggableRegionsChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    const std::vector<CefDraggableRegion> &regions) {
  if (!definition_.desktop || !frame->IsMain()) return;
  const auto view = CefBrowserView::GetForBrowser(browser);
  if (view && view->GetWindow()) view->GetWindow()->SetDraggableRegions(regions);
}
}
