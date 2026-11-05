/** @file app_runtime.cpp 注册受信内置目录，生命周期以真实 CEF 回调为准。 */
#include "app_runtime.h"
#include "app_context.h"
#include "browser_data.h"
#include "workspace_files.h"
#include "package_manager.h"
#include "host_client.h"
#include "host_window.h"
#include "data_store.h"
#include "function_task.h"
#include "package_policy.h"
#include "url_policy.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include <algorithm>

namespace alkaid {
AppRuntime::AppRuntime(std::filesystem::path root, std::filesystem::path dataRoot)
  : root_(std::move(root)), dataRoot_(std::move(dataRoot)) {}
AppRuntime::~AppRuntime() {
  alive_->store(false);
  if (windowWorker_) windowWorker_->Stop();
}
bool AppRuntime::initialize(std::string &error) {
  CEF_REQUIRE_UI_THREAD();
  browserCleanupFailures_ = BrowserData::applyPending(dataRoot_);
  // 此目录由构建产物决定；应用包安装服务以后通过独立校验流程登记，页面不能指定路径或角色。
  const AppDefinition definitions[] = {
    {UrlPolicy::DESKTOP_APP_ID, "desktop", "瑶光台桌面", "2.0.0-dev.6", true},
    {"org.alkaid.inspector", "inspector", "能力查看器", "1.0.0", false},
    {WorkspaceFiles::APP_ID, "files", "文件", "1.0.0", false, false, {"workspace.files"}, {"workspace.files"}},
    {.appId = "org.alkaid.notes", .originKey = "notes", .name = "笺 · 私有便签", .version = "1.1.0",
      .permissions = {"storage.kv", "files.private", "files.userSelected.read", "files.userSelected.write"},
      .grants = {"storage.kv", "files.private", "files.userSelected.read", "files.userSelected.write"},
      .window = {.width = 1180, .height = 780, .minWidth = 760, .minHeight = 560}}
  };
  for (const auto &definition : definitions) {
    auto assets = ResourceService::load(definition.desktop ? root_ / "web" : root_ / "web" / "samples" / definition.originKey,
        error, definition.desktop, definition.originKey == "notes" ? "notes.css" : "");
    if (!assets) return false;
    entries_.emplace(definition.appId, Entry{definition, std::move(assets), nullptr, nullptr, "stopped"});
  }
  packages_ = std::make_unique<PackageManager>(this, root_, dataRoot_);
  packages_->initialize();
  windowStore_ = std::make_shared<DataStore>(dataRoot_ / "config" / "window", true);
  windowWorker_ = CefThread::CreateThread("alkaid-window-layout");
  const auto failure = start(entries_.at(UrlPolicy::DESKTOP_APP_ID));
  if (failure) { error = failure->message; return false; }
  return true;
}

CefRefPtr<CefDictionaryValue> AppRuntime::describe(const Entry &entry) const {
  auto value = CefDictionaryValue::Create();
  value->SetString("appId", entry.definition.appId);
  value->SetString("name", entry.definition.name);
  value->SetString("version", entry.definition.version);
  value->SetString("kind", entry.definition.desktop ? "desktop" : entry.definition.installed ? "installed" : "bundled");
  value->SetString("state", entry.state);
  value->SetBool("canRollback", entry.canRollback);
  if (entry.overlay) {
    value->SetBool("workspaceActive", workspaceApp_ == entry.definition.appId);
    const auto bounds = entry.bounds.value_or(CefRect(0, 0, entry.definition.window.width, entry.definition.window.height));
    auto layout = CefDictionaryValue::Create();
    layout->SetInt("x", bounds.x); layout->SetInt("y", bounds.y);
    layout->SetInt("width", bounds.width); layout->SetInt("height", bounds.height);
    layout->SetInt("minWidth", entry.definition.window.minWidth);
    layout->SetInt("minHeight", entry.definition.window.minHeight);
    layout->SetBool("resizable", entry.definition.window.resizable);
    value->SetDictionary("workspaceBounds", layout);
  }
  if (entry.definition.installed) {
    value->SetBool("browserCleanupPending", BrowserData::pending(dataRoot_, entry.definition.originKey));
    value->SetBool("browserCleanupFailed", browserCleanupFailures_.contains(entry.definition.originKey));
  }
  value->SetString("publisherName", entry.definition.publisherName);
  if (!entry.definition.icon.empty()) value->SetString("icon", entry.definition.icon);
  value->SetString("publisherKeyId", entry.definition.publisherKeyId);
  auto declared = CefListValue::Create();
  auto granted = CefListValue::Create();
  for (const auto &permission : entry.definition.permissions) {
    declared->SetString(declared->GetSize(), permission);
    if (entry.definition.grants.contains(permission)) granted->SetString(granted->GetSize(), permission);
  }
  value->SetList("declaredPermissions", declared);
  value->SetList("grantedPermissions", granted);
  const auto endpoints = CefParseJSON(entry.definition.declaredNetworkEndpoints.dump(), JSON_PARSER_RFC);
  if (endpoints && endpoints->GetType() == VTYPE_LIST) value->SetList("networkEndpoints", endpoints->GetList());
  auto bindings = CefDictionaryValue::Create();
  for (const auto &[pluginId, binding] : entry.definition.pluginBindings) bindings->SetString(pluginId, binding.version);
  value->SetDictionary("pluginBindings", bindings);
  value->SetString("error", entry.loadError.empty()?entry.closeMessage:entry.loadError);
  return value;
}

CefRefPtr<CefListValue> AppRuntime::list() const {
  CEF_REQUIRE_UI_THREAD();
  auto result = CefListValue::Create();
  std::size_t index = 0;
  for (const auto &[id, entry] : entries_) result->SetDictionary(index++, describe(entry));
  return result;
}

std::optional<RuntimeError> AppRuntime::start(Entry &entry) {
  entry.state = "starting";
  entry.closeHandler=false; entry.closeReady=false; entry.closeApproved=false; entry.closeParticipant=false; entry.closeTicket.clear(); entry.closeMessage.clear();
  if (entry.context) { contextReady(entry.definition.appId, entry.context); return std::nullopt; }
  const auto directory = dataRoot_ / "profiles" / entry.definition.originKey;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    entry.state = "stopped";
    return RuntimeError{"APP_START_FAILED", "无法创建应用数据目录"};
  }
  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = directory.wstring();
  entry.context = CefRequestContext::CreateContext(settings, new AppContext(this, entry.definition.appId));
  if (!entry.context) {
    entry.state = "stopped";
    return RuntimeError{"APP_START_FAILED", "无法创建应用上下文"};
  }
  return std::nullopt;
}

void AppRuntime::contextReady(const std::string &appId, CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  auto &entry = entries_.at(appId);
  if (shuttingDown_ || entry.state == "closing") {
    entry.state = "stopped";
    maybeQuit();
    return;
  }
  if (!entry.boundsLoaded && windowWorker_) {
    const auto alive = alive_;
    const auto key = entry.definition.originKey;
    const bool posted = windowWorker_->GetTaskRunner()->PostTask(new FunctionTask([this,alive,appId,key,context,store=windowStore_] {
      DataLease lease;
      lease.deadline = RuntimeClock::now() + std::chrono::seconds(5);
      const auto result = store->call("storage.get", {{"key",key}}, 0, lease);
      std::optional<CefRect> bounds;
      if (const auto item = std::get_if<Json>(&result); item && (*item)["found"] == true) {
        const auto &value = (*item)["value"];
        if (PackagePolicy::keys(value, {"x","y","width","height"}) &&
            std::ranges::all_of(value, [](const auto &part) { return part.is_number_integer() && part >= -100000 && part <= 100000; }) &&
            value["width"] > 0 && value["height"] > 0)
          bounds.emplace(value["x"].get<int>(), value["y"].get<int>(), value["width"].get<int>(), value["height"].get<int>());
      }
      CefPostTask(TID_UI, new FunctionTask([this,alive,appId,context,bounds] {
        if (!alive->load()) return;
        const auto found = entries_.find(appId);
        if (found == entries_.end()) return;
        found->second.boundsLoaded = true;
        found->second.bounds = bounds;
        if (found->second.state == "starting" || found->second.state == "closing") contextReady(appId, context);
      }));
    }));
    if (posted) return;
  }
  entry.boundsLoaded = true;
  const auto &definition = entry.definition;
  context->RegisterSchemeHandlerFactory("alkaid", definition.originKey, new ResourceService(entry.assets, definition.originKey));
  CefBrowserSettings settings;
  auto extra = CefDictionaryValue::Create();
  extra->SetString("originKey", definition.originKey);
  CefRefPtr<HostWindow> delegate = new HostWindow(this,definition.appId,definition.name,definition.window,entry.bounds,definition.desktop,root_ / "web" / "assets");
  const auto view = CefBrowserView::CreateBrowserView(new HostClient(this, definition),
    "alkaid://" + definition.originKey + "/" + definition.entry, settings, extra, context, delegate);
  if (!view) { entry.state = "stopped"; maybeQuit(); return; }
  if (!definition.desktop) {
    const auto &desktop = entries_.at(UrlPolicy::DESKTOP_APP_ID);
    const auto desktopView = desktop.browser ? CefBrowserView::GetForBrowser(desktop.browser) : nullptr;
    const auto window = desktopView ? desktopView->GetWindow() : nullptr;
    if (!window) { entry.state = "stopped"; entry.closeMessage = "桌面窗口尚未就绪"; return; }
    entry.overlay = window->AddOverlayView(view, CEF_DOCKING_MODE_CUSTOM, true);
    if (!entry.overlay) { entry.state = "stopped"; entry.closeMessage = "无法创建桌面内窗口"; return; }
    focus(entry);
    return;
  }
  delegate->setBrowserView(view);
  CefWindow::CreateTopLevelWindow(delegate);
}

void AppRuntime::rememberWindowBounds(const std::string &appId, const CefRect &bounds) {
  auto &entry = entries_.at(appId);
  if (bounds.width <= 0 || bounds.height <= 0 || entry.bounds == bounds) return;
  entry.bounds = bounds;
  entry.boundsDirty = true;
  if (entry.boundsTimer || !windowWorker_) return;
  const auto alive = alive_;
  entry.boundsTimer = CefPostDelayedTask(TID_UI, new FunctionTask([this,alive,appId] {
    if (!alive->load()) return;
    const auto found = entries_.find(appId);
    if (found == entries_.end()) return;
    found->second.boundsTimer = false;
    flushWindowBounds(appId);
  }), 300);
}

void AppRuntime::flushWindowBounds(const std::string &appId) {
  const auto found = entries_.find(appId);
  if (!windowWorker_ || found == entries_.end() || !found->second.boundsDirty || !found->second.bounds) return;
  auto &entry = found->second;
  const auto bounds = *entry.bounds;
  const auto key = entry.definition.originKey;
  if (windowWorker_->GetTaskRunner()->PostTask(new FunctionTask([store=windowStore_,key,bounds] {
    DataLease lease;
    lease.deadline = RuntimeClock::now() + std::chrono::seconds(5);
    store->call("storage.set", {{"key",key},{"value",{{"x",bounds.x},{"y",bounds.y},{"width",bounds.width},{"height",bounds.height}}}}, 0, lease);
  }))) entry.boundsDirty = false;
}

void AppRuntime::focus(Entry &entry) {
  if (entry.overlay) {
    if (workspaceApp_ != entry.definition.appId) {
      const auto previous = entries_.find(workspaceApp_);
      if (previous != entries_.end() && previous->second.overlay && previous->second.overlay->IsValid())
        previous->second.overlay->SetVisible(false);
      workspaceApp_ = entry.definition.appId;
    }
    if (entry.overlay->IsValid() && entry.overlay->IsVisible() && entry.browser) {
      if (const auto window = entry.overlay->GetWindow()) window->Activate();
      entry.browser->GetHost()->SetFocus(true);
    }
    return;
  }
  const auto view = entry.browser ? CefBrowserView::GetForBrowser(entry.browser) : nullptr;
  const auto window = view ? view->GetWindow() : nullptr;
  if (window) {
    if (window->IsMinimized()) window->Restore();
    window->Show();
    window->Activate();
    view->RequestFocus();
  }
}

RuntimeResult<Json> AppRuntime::layoutWorkspace(const Json &params) {
  CEF_REQUIRE_UI_THREAD();
  const auto appId = params["appId"].get<std::string>();
  const auto found = entries_.find(appId);
  if (found == entries_.end() || !found->second.overlay || !found->second.overlay->IsValid() || workspaceApp_ != appId)
    return RuntimeError{"APP_NOT_RUNNING", "应用当前未在桌面前台"};
  auto &entry = found->second;
  const auto overlay = entry.overlay;
  if (!params["visible"].get<bool>()) {
    overlay->SetVisible(false);
    if (params["minimize"].get<bool>()) workspaceApp_.clear();
    return Json(nullptr);
  }
  const auto window = overlay->GetWindow();
  if (!window) return RuntimeError{"APP_NOT_RUNNING", "桌面窗口尚未建立"};
  const auto area = window->GetSize();
  const auto x = std::clamp(params["x"].get<int>(), 0, std::max(0, area.width - 1));
  const auto y = std::clamp(params["y"].get<int>(), 0, std::max(0, area.height - 1));
  const int width = std::clamp(params["width"].get<int>(), 1, std::max(1, area.width));
  const int height = std::clamp(params["height"].get<int>(), 1, std::max(1, area.height));
  const CefRect rect(x, y, std::clamp(width, 1, std::max(1, area.width - x)),
    std::clamp(height, 1, std::max(1, area.height - y)));
  overlay->SetBounds(rect);
  overlay->SetVisible(true);
  // 窗口部分移出可见区时仍保存原始大小。
  if (params["persist"].get<bool>()) rememberWindowBounds(appId, CefRect(x, y, width, height));
  if (params["focus"].get<bool>() && entry.browser) {
    window->Activate();
    entry.browser->GetHost()->SetFocus(true);
  }
  return Json(nullptr);
}

void AppRuntime::hideWorkspace(Entry &entry) {
  if (workspaceApp_ != entry.definition.appId) return;
  workspaceApp_.clear();
  if (entry.overlay && entry.overlay->IsValid()) entry.overlay->SetVisible(false);
  emit(entries_.at(UrlPolicy::DESKTOP_APP_ID), "workspace.hidden", {{"appId", entry.definition.appId}});
}

RuntimeResult<CefRefPtr<CefDictionaryValue>> AppRuntime::act(const std::string &method, const std::string &appId) {
  CEF_REQUIRE_UI_THREAD();
  if (shuttingDown_ || exitPending_) return RuntimeError{"RUNTIME_STOPPING", "瑶光台正在协商退出"};
  if (packages_ && packages_->locks(appId)) return RuntimeError{"APP_BUSY", "应用正在执行管理操作"};
  const auto found = entries_.find(appId);
  if (found == entries_.end()) return RuntimeError{"APP_NOT_FOUND", "应用未注册"};
  auto &entry = found->second;
  if (method == "apps.launch" && packages_ && std::ranges::any_of(entry.definition.pluginDependencies,
      [&](const auto &id) { return packages_->locksPlugin(id); }))
    return RuntimeError{"APP_BUSY", "应用依赖的插件正在变更"};
  if (entry.definition.desktop) return RuntimeError{"PROTECTED_APP", "桌面由平台管理，请使用平台退出入口"};
  if (entry.state == "unavailable") return RuntimeError{"APP_UNAVAILABLE", entry.loadError};
  if (method == "apps.launch" && entry.definition.installed && BrowserData::pending(dataRoot_, entry.definition.originKey))
    return browserCleanupFailures_.contains(entry.definition.originKey)
      ? RuntimeError{"APP_BROWSER_CLEANUP_FAILED", "浏览器数据清理失败，请解除文件占用或检查目录权限，再重启瑶光台"}
      : RuntimeError{"APP_BROWSER_CLEANUP_PENDING", "请退出并重开瑶光台，完成浏览器数据清理后再启动应用"};
  if (method == "apps.close") {
    requestClose(entry,"manager");
  } else {
    if (entry.state == "closing") return RuntimeError{"APP_BUSY", "应用正在关闭，请稍后重试"};
    if (method == "apps.focus" && entry.state != "running") return RuntimeError{"APP_NOT_RUNNING", "应用尚未运行"};
    if (entry.state == "stopped") {
      if (const auto error = start(entry)) return *error;
    } else if (entry.state == "running") focus(entry);
  }
  return describe(entry);
}

void AppRuntime::browserCreated(const std::string &appId, CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto &entry = entries_.at(appId);
  entry.browser = browser;
  if (shuttingDown_ || entry.state == "closing") browser->GetHost()->CloseBrowser(true);
  else entry.state = "running";
}

void AppRuntime::browserClosed(const std::string &appId) {
  CEF_REQUIRE_UI_THREAD();
  auto &entry = entries_.at(appId);
  entry.browser = nullptr;
  if (entry.overlay) {
    hideWorkspace(entry);
    flushWindowBounds(appId);
    const auto overlay = std::exchange(entry.overlay, nullptr);
    CefPostTask(TID_UI, new FunctionTask([overlay] { if (overlay->IsValid()) overlay->Destroy(); }));
  }
  entry.state = "stopped";
  if (entry.definition.desktop && !shuttingDown_) {
    // 浏览器强制关闭（如调试器 Page.close）已不可撤回；桌面消失后不能留下无管理入口的平台。
    // 正常原生关闭与 runtime.exit 会在销毁任何窗口之前完成 shutdown 的协商。
    exitPending_=false; shuttingDown_=true;
    if (packages_) packages_->stop();
    for (auto &[id,other]:entries_) if (other.state!="stopped" && other.state!="unavailable") finishClose(other);
  }
  maybeQuit();
}

void AppRuntime::maybeQuit() {
  if (!shuttingDown_) return;
  if (packages_ && packages_->working()) return;
  for (const auto &[id, entry] : entries_)
    if (!entry.definition.desktop && entry.state != "stopped" && entry.state != "unavailable") return;
  auto &desktop = entries_.at(UrlPolicy::DESKTOP_APP_ID);
  if (desktop.state != "stopped") {
    if (!desktop.closeApproved) finishClose(desktop);
    return;
  }
  CefQuitMessageLoop();
}
RuntimeResult<Json> AppRuntime::packageCall(const std::string &method, const Json &params, CefRefPtr<CefBrowser> browser) {
  if (exitPending_ && method != "apps.getTask" && method != "apps.listTasks") return RuntimeError{"RUNTIME_STOPPING","平台正在协商退出"};
  return packages_->call(method, params, browser);
}
bool AppRuntime::canModifyPackage(const std::string &appId) const {
  const auto found = entries_.find(appId);
  return found == entries_.end() || (found->second.definition.installed &&
    (found->second.state == "stopped" || found->second.state == "unavailable"));
}
bool AppRuntime::isInstalledPackage(const std::string &appId) const {
  const auto found = entries_.find(appId);
  return found != entries_.end() && found->second.definition.installed;
}
std::string AppRuntime::packageOriginKey(const std::string &appId) const {
  return entries_.at(appId).definition.originKey;
}
bool AppRuntime::pluginInUse(const std::string &pluginId) const {
  return std::ranges::any_of(entries_, [&](const auto &item) {
    const auto &entry = item.second;
    return entry.state != "stopped" && entry.state != "unavailable" &&
      entry.definition.pluginDependencies.contains(pluginId);
  });
}

void AppRuntime::applyPackages(const PackageSnapshot &snapshot) {
  CEF_REQUIRE_UI_THREAD();
  if (shuttingDown_) return;
  std::set<std::string> active;
  for (const auto &app : snapshot) {
    active.insert(app.appId);
    auto found = entries_.find(app.appId);
    if (found != entries_.end() && !found->second.definition.installed) continue;
    if (found == entries_.end()) found = entries_.emplace(app.appId, Entry{}).first;
    auto &entry = found->second;
    // 运行中应用保留其绑定的资源；只有停用状态才能由包任务切换版本。
    if (entry.state != "stopped" && entry.state != "unavailable") continue;
    entry.definition = {app.appId, app.originKey, app.name, app.version, false, true, app.package ? app.package->permissions : std::set<std::string>{}, app.grants};
    entry.definition.publisherKeyId = app.publisherKeyId;
    if (app.package) {
      entry.definition.publisherName = app.package->publisherName;
      entry.definition.icon = app.package->icon;
      entry.definition.entry = app.package->entry;
      entry.definition.window = app.package->window;
      for (const auto &dependency : app.package->dependencies)
        entry.definition.pluginDependencies.insert(dependency["pluginId"].get<std::string>());
      entry.definition.pluginBindings = app.pluginBindings;
      entry.definition.declaredNetworkEndpoints = app.package->networkEndpoints;
      for (const auto &endpoint : app.package->networkEndpoints)
        if (app.grants.contains(endpoint["scope"].get<std::string>())) entry.definition.networkEndpoints.push_back(endpoint);
    }
    entry.canRollback = app.canRollback;
    entry.loadError = app.error;
    const bool available = app.package && !app.dependencyError;
    entry.state = available ? "stopped" : "unavailable";
    entry.assets = available ? ResourceService::fromPackage(app.package) : nullptr;
  }
  std::erase_if(entries_, [&](const auto &pair) {
    return pair.second.definition.installed && !active.contains(pair.first) &&
      (pair.second.state == "stopped" || pair.second.state == "unavailable");
  });
}
} // namespace alkaid
