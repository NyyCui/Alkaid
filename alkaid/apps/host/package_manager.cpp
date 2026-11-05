/** @file package_manager.cpp 工作线程只持有包与注册表，UI 线程管理任务状态和窗口。 */
#include "package_manager.h"
#include "plugin_process.h"
#include "app_runtime.h"
#include "browser_data.h"
#include "local_publisher.h"
#include "publisher_backup.h"
#include "runtime_audit.h"
#include "function_task.h"
#include "crypto.h"
#include "package_policy.h"
#include "safe_files.h"
#include "include/wrapper/cef_helpers.h"
#include <fstream>
#include <shellapi.h>

namespace alkaid {
namespace _ {
std::optional<RuntimeError> checkPackEntry(const std::filesystem::path &source, const std::string &entry) {
  auto current = source;
  if (!PackageFiles::safe(source))
    return RuntimeError{"PACK_ENTRY_UNSAFE", "构建目录包含链接，请选择实际的构建目录。"};
  const auto missing = RuntimeError{"PACK_ENTRY_MISSING", "构建目录中找不到入口「" + entry + "」，请检查目录和文件名大小写。"};
  for (const auto &part : std::filesystem::path(entry)) {
    current /= part;
    WIN32_FIND_DATAW item{};
    const auto found = FindFirstFileW(current.c_str(), &item);
    if (found == INVALID_HANDLE_VALUE) return missing;
    FindClose(found);
    if (part.native() != item.cFileName) return missing;
    if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      return RuntimeError{"PACK_ENTRY_UNSAFE", "入口路径包含链接，请选择实际的入口文件。"};
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(current, error)) return missing;
  const auto size = std::filesystem::file_size(current, error);
  if (error || !std::ifstream(current, std::ios::binary))
    return RuntimeError{"PACK_ENTRY_UNREADABLE", "无法读取入口文件，请检查文件访问权限。"};
  if (size > PackagePolicy::MAX_FILE) return RuntimeError{"PACK_ENTRY_TOO_LARGE", "入口文件超过 8 MiB，请缩小文件后重试。"};
  return std::nullopt;
}
/** 清除该应用的宿主私有存储和应用配置。目录只由已注册的应用标识推导。 */
std::optional<RuntimeError> clearAppData(const std::filesystem::path &root, const std::string &origin) {
  const auto privateRoot = root / "private";
  const auto configRoot = root / "config" / "apps";
  if (!PackageFiles::safe(privateRoot / origin) || !PackageFiles::safe(configRoot / origin))
    return RuntimeError{"APP_DATA_PATH_UNSAFE", "应用数据路径含有重解析点，无法清理"};
  if (!PackageFiles::removeTree(privateRoot, privateRoot / origin) ||
      !PackageFiles::removeTree(configRoot, configRoot / origin))
    return RuntimeError{"APP_DATA_CLEAR_FAILED", "部分数据可能已删除，请检查文件占用后重试"};
  return std::nullopt;
}

Json pluginList(const PackageSnapshot &snapshot) {
  Json result = Json::array();
  for (const auto &item : snapshot)
    result.push_back({{"pluginId",item.appId},{"name",item.name},{"version",item.version},
      {"publisherKeyId",item.publisherKeyId},{"error",item.error}});
  return result;
}
class PackPicker : public CefRunFileDialogCallback {
  IMPLEMENT_REFCOUNTING(PackPicker);
public:
  explicit PackPicker(std::function<void(std::filesystem::path)> receive) : receive_(std::move(receive)) {}
  void OnFileDialogDismissed(const std::vector<CefString> &paths) override {
    auto path = paths.empty() ? std::filesystem::path{} : std::filesystem::path(paths.front().ToWString());
    CefPostTask(TID_UI, new FunctionTask([receive = receive_, path = std::move(path)] { receive(path); }));
  }
private:
  std::function<void(std::filesystem::path)> receive_;
};

std::wstring quoteArg(const std::filesystem::path &path) {
  auto value = path.wstring();
  const auto last = value.find_last_not_of(L'\\');
  value.append(last == std::wstring::npos ? value.size() : value.size() - last - 1, L'\\');
  return L"\"" + value + L"\"";
}

std::optional<RuntimeError> runPacker(const std::filesystem::path &tools, const std::filesystem::path &manifest,
    const std::vector<std::filesystem::path> &paths, bool localPublisher) {
  wchar_t system[MAX_PATH]{};
  const auto length = GetSystemDirectoryW(system, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return RuntimeError{"PACK_TOOL_UNAVAILABLE", "无法定位 Windows PowerShell"};
  const auto powershell = std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe";
  auto errorPath = manifest;
  errorPath.replace_extension(".error");
  std::error_code configError;
  const bool installed = std::filesystem::exists(tools.parent_path() / "data-location.txt", configError);
  const std::filesystem::path node = installed ? std::filesystem::path(L"node.exe") : std::filesystem::path(ALKAID_PACK_NODE);
  auto command = quoteArg(powershell) + L" -NoProfile -NonInteractive -File " + quoteArg(tools / "pack-ypkg.ps1") +
    L" -ManifestPath " + quoteArg(manifest) + L" -AssetsPath " + quoteArg(paths[0]) +
    L" -SigningKeyPath " + quoteArg(paths[1]) + L" -OutputPath " + quoteArg(paths[2]) +
    L" -NodePath " + quoteArg(node) + L" -VerifierPath " + quoteArg(tools / "alkaid_package_tool.exe") +
    L" -ErrorPath " + quoteArg(errorPath) + (localPublisher ? L" -LocalPublisher" : L"");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const auto job = CreateJobObjectW(nullptr, nullptr);
  if (!job) return RuntimeError{"PACK_TOOL_UNAVAILABLE", "无法建立打包进程的资源边界"};
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, tools.c_str(), &startup, &process)) {
    CloseHandle(job);
    return RuntimeError{"PACK_TOOL_UNAVAILABLE", "无法启动开发打包工具"};
  }
  if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == DWORD(-1)) {
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return RuntimeError{"PACK_TOOL_UNAVAILABLE", "无法启动受控打包进程"};
  }
  const auto waited = WaitForSingleObject(process.hProcess, 120000);
  DWORD code = 1;
  if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(job);
  if (waited == WAIT_TIMEOUT) return RuntimeError{"PACK_TIMEOUT", "打包工具运行超时"};
  if (code != 0) {
    std::ifstream errorFile(errorPath, std::ios::binary);
    std::string message(4096, '\0');
    errorFile.read(message.data(), static_cast<std::streamsize>(message.size()));
    message.resize(static_cast<std::size_t>(errorFile.gcount()));
    errorFile.close();
    std::error_code ignored;
    std::filesystem::remove(errorPath, ignored);
    if (message.empty()) message = "打包或验签失败，请检查开发工具链及 PowerShell 执行策略";
    return RuntimeError{"PACK_FAILED", message};
  }
  return std::nullopt;
}

class PackagePicker : public CefRunFileDialogCallback {
  IMPLEMENT_REFCOUNTING(PackagePicker);
public:
  PackagePicker(PackageManager *manager, std::shared_ptr<std::atomic_bool> alive, std::string id)
    : manager_(manager), alive_(std::move(alive)), id_(std::move(id)) {}
  void OnFileDialogDismissed(const std::vector<CefString> &paths) override {
    if (alive_->load()) manager_->selected(id_, paths.empty() ? std::filesystem::path{} : std::filesystem::path(paths[0].ToWString()));
  }
private:
  PackageManager *manager_;
  std::shared_ptr<std::atomic_bool> alive_;
  std::string id_;
};
}
PackageManager::PackageManager(AppRuntime *runtime, std::filesystem::path root, std::filesystem::path dataRoot)
  : runtime_(runtime), toolsRoot_(std::move(root) / "developer"), dataRoot_(std::move(dataRoot)),
    pluginStore_(std::make_shared<PackageStore>(dataRoot_ / "plugins", nullptr, PackageStore::Kind::Plugin)),
    store_(std::make_shared<PackageStore>(dataRoot_ / "packages", nullptr, PackageStore::Kind::Application, pluginStore_)),
    worker_(CefThread::CreateThread("alkaid-packages")) {}
PackageManager::~PackageManager() {
  alive_->store(false);
  if (worker_) worker_->Stop();
}
void PackageManager::initialize() {
  if (!worker_) { startupError_ = RuntimeError{"WORKER_UNAVAILABLE","无法启动应用包工作线程"}; return; }
  working_ = true;
  const auto store = store_;
  const auto plugins = pluginStore_;
  const auto alive = alive_;
  worker_->GetTaskRunner()->PostTask(new FunctionTask([this, store, plugins, alive] {
    auto pluginError = plugins->initialize();
    auto installedPlugins = Json::array();
    if (!pluginError) {
      const auto loaded = plugins->load(true);
      if (const auto failure = std::get_if<RuntimeError>(&loaded)) pluginError = *failure;
      else installedPlugins = _::pluginList(std::get<PackageSnapshot>(loaded));
    }
    const auto error = store->initialize();
    RuntimeResult<PackageSnapshot> snapshot = error ? RuntimeResult<PackageSnapshot>(*error) : store->load(true);
    const auto history = error ? Json::array() : store->history();
    CefPostTask(TID_UI, new FunctionTask([this, alive, snapshot = std::move(snapshot), history, pluginError, installedPlugins] {
      if (!alive->load()) return;
      working_ = false;
      pluginError_ = pluginError;
      plugins_ = installedPlugins;
      if (const auto error = std::get_if<RuntimeError>(&snapshot)) startupError_ = *error;
      else { runtime_->applyPackages(std::get<PackageSnapshot>(snapshot)); ready_ = true; }
      for (auto it = history.rbegin(); it != history.rend(); ++it) {
        const auto &record = *it;
        const auto id = record["taskId"].get<std::string>();
        tasks_.emplace(id, Task{record, std::nullopt}); order_.push_back(id);
      }
      runtime_->packageWorkFinished();
    }));
  }));
}
std::string PackageManager::createTask(const std::string &kind, const std::string &appId) {
  std::array<unsigned char, 32> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
  const auto id = PackageCrypto::hex(bytes);
  if (order_.size() >= 32) { tasks_.erase(order_.front()); order_.erase(order_.begin()); }
  tasks_.emplace(id, Task{{{"taskId",id},{"kind",kind},{"appId",appId},{"status","running"},{"phase","queued"},{"message","等待处理"}},std::nullopt});
  order_.push_back(id);
  return id;
}
void PackageManager::fail(const std::string &id, RuntimeError error) {
  auto &task = tasks_.at(id);
  task.info["status"] = "failed";
  task.info["phase"] = "failed";
  task.info["message"] = error.message;
  task.info["error"] = {{"code",error.code},{"message",error.message}};
  RuntimeAudit::record(dataRoot_, task.info["kind"].get<std::string>(), task.info["appId"].get<std::string>(), "", error.code);
  task.preview.reset();
  if (activePreview_ == id) activePreview_.clear();
}
RuntimeResult<Json> PackageManager::call(const std::string &method, const Json &params, CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (method == "apps.listAudit") return RuntimeAudit::list(dataRoot_);
  if (startupError_) return *startupError_;
  if (method == "apps.listTasks") {
    Json result = Json::array();
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) result.push_back(tasks_.at(*it).info);
    return result;
  }
  if (method == "apps.advancePack" || method == "apps.cancelPack" || method == "apps.revealPack" || method == "apps.installPacked") {
    const auto id = params["taskId"].get<std::string>();
    const auto found = tasks_.find(id);
    if (found == tasks_.end() || !found->second.pack) return RuntimeError{"TASK_NOT_FOUND", "打包任务已失效"};
    const auto job = found->second.pack;
    auto &info = found->second.info;
    if (stopping_) return RuntimeError{"RUNTIME_STOPPING", "平台正在退出"};
    if (method == "apps.revealPack" || method == "apps.installPacked") {
      if (info["status"] != "succeeded" || job->paths.size() != 3) return RuntimeError{"TASK_NOT_READY", "安装包尚未生成成功"};
      if (method == "apps.revealPack") {
        const auto folder = job->paths[2].parent_path();
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
          return RuntimeError{"FILE_IO", "无法打开保存目录"};
        return Json{{"taskId", id}};
      }
      if (working_ || !activePreview_.empty()) return RuntimeError{"OPERATION_BUSY", "请先完成当前包操作"};
      const bool plugin = PackagePolicy::json(job->manifest).value("type", "app") == "plugin";
      const auto installId = createTask(plugin ? "pluginInstall" : "install");
      if (installId.empty()) return RuntimeError{"RESOURCE_EXHAUSTED", "无法创建安装任务"};
      activePreview_ = installId;
      selected(installId, job->paths[2]);
      return Json{{"taskId", installId}};
    }
    if (info["status"] != "awaitingInput" || activePreview_ != id)
      return RuntimeError{"TASK_NOT_READY", "当前步骤正在处理，请等待结果"};
    if (method == "apps.cancelPack") {
      info["status"] = info["phase"] = "cancelled";
      info["message"] = "已返回编辑，应用信息已保留";
      activePreview_.clear();
    } else if (job->paths.size() == 3) buildPackage(job);
    else pickPackFile(browser, job);
    return Json{{"taskId", id}};
  }
  if (method == "apps.getTask") {
    const auto found = tasks_.find(params["taskId"].get<std::string>());
    if (found == tasks_.end()) return RuntimeError{"TASK_NOT_FOUND","任务不存在或已超出保留范围"};
    return found->second.info;
  }
  if (stopping_) return RuntimeError{"RUNTIME_STOPPING","平台正在退出"};
  if (!ready_) return RuntimeError{"RUNTIME_NOT_READY","应用注册表尚未加载完成"};
  if (method.starts_with("plugins.") && pluginError_) return *pluginError_;
  if (method == "plugins.list") return plugins_;
  if (method == "apps.publisherBackup") {
    if (working_ || !activePreview_.empty()) return RuntimeError{"OPERATION_BUSY","请先完成当前包操作"};
    const bool restore = params["restore"].get<bool>();
    const auto id = createTask("publisherBackup");
    if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED","无法分配任务标识"};
    activePreview_ = id;
    auto &info = tasks_.at(id).info;
    info["phase"] = "choosing";
    info["message"] = restore ? "请选择发布者备份" : "请选择备份保存位置";
    const auto alive = alive_;
    auto password = std::make_shared<std::string>(params["password"].get<std::string>());
    browser->GetHost()->RunFileDialog(restore ? FILE_DIALOG_OPEN : FILE_DIALOG_SAVE,
      restore ? "选择发布者备份" : "保存发布者备份", restore ? "" : "publisher.ypkey", {".ypkey"},
      new _::PackPicker([this,alive,id,restore,password](std::filesystem::path file) {
        if (!alive->load() || stopping_ || file.empty()) {
          SecureZeroMemory(password->data(), password->size());
          if (!alive->load()) return;
          auto &info = tasks_.at(id).info;
          info["status"] = info["phase"] = "cancelled";
          info["message"] = "已取消发布者备份操作";
          activePreview_.clear();
          return;
        }
        working_ = true;
        tasks_.at(id).info["phase"] = "committing";
        const bool posted = worker_->GetTaskRunner()->PostTask(new FunctionTask([this,alive,id,restore,password,file] {
          const auto error = PublisherBackup::run(dataRoot_, file, *password, restore);
          SecureZeroMemory(password->data(), password->size());
          CefPostTask(TID_UI, new FunctionTask([this,alive,id,restore,error] {
            if (!alive->load()) return;
            working_ = false;
            activePreview_.clear();
            if (error) fail(id, *error);
            else {
              auto &info = tasks_.at(id).info;
              info["status"] = "succeeded";
              info["phase"] = "committed";
              info["message"] = restore ? "发布者已恢复" : "已保存加密发布者备份";
              RuntimeAudit::record(dataRoot_, "publisherBackup", "", "", "OK");
            }
            runtime_->packageWorkFinished();
          }));
        }));
        if (!posted) {
          SecureZeroMemory(password->data(), password->size());
          working_ = false;
          fail(id, {"RUNTIME_STOPPING", "应用管理工作线程不可用"});
        }
      }));
    return Json{{"taskId",id}};
  }
  if (method == "apps.localPublisher") {
    if (!params.contains("name")) return LocalPublisher::read(dataRoot_);
    if (working_ || !activePreview_.empty()) return RuntimeError{"OPERATION_BUSY","请先完成当前包操作"};
    const auto name = params["name"].get<std::string>();
    if (name.find_first_not_of(' ') == std::string::npos ||
        std::ranges::any_of(name, [](unsigned char c) { return c < 32 || c == 127; }))
      return RuntimeError{"INVALID_REQUEST", "请输入有效的发布者名称"};
    return LocalPublisher::create(dataRoot_, name);
  }
  if (method == "apps.preparePack") {
    if (working_ || !activePreview_.empty()) return RuntimeError{"OPERATION_BUSY","请先结束当前包操作"};
    const auto manifest = params["manifest"].get<std::string>();
    auto parsed = PackagePolicy::json(manifest);
    if (!parsed.is_object() || parsed.value("manifestVersion", Json()) != 3 ||
        (parsed.value("type", Json()) != "app" && parsed.value("type", Json()) != "plugin"))
      return RuntimeError{"INVALID_REQUEST","请输入 YPKG v3 应用或插件清单"};
    const auto field = [&parsed](const char *key) { return parsed.contains(key) && parsed[key].is_string() ? parsed[key].get<std::string>() : std::string{}; };
    const bool plugin = parsed["type"] == "plugin";
    if (parsed.contains("icon") && (plugin || !PackagePolicy::appIcon(field("icon"))))
      return RuntimeError{"INVALID_REQUEST", "清单 icon 须为有界 WebP Base64，仅应用包支持图标。"};
    const auto entry = field("entry");
    const auto name = field("name");
    if (name.empty() || name.size() > 128 || name.find_first_not_of(' ') == std::string::npos ||
        std::ranges::any_of(name, [](unsigned char c) { return c < 32 || c == 127; }))
      return RuntimeError{"INVALID_REQUEST", "清单 name 须为非空名称，最多 128 个 UTF-8 字节，且不能包含控制字符。"};
    if (!PackagePolicy::appId(field(plugin ? "pluginId" : "appId")))
      return RuntimeError{"INVALID_REQUEST", plugin ? "清单 pluginId 无效，例如 com.example.plugin。" : "清单 appId 无效，例如 com.example.app。"};
    if (!PackagePolicy::version(field("version")))
      return RuntimeError{"INVALID_REQUEST", "清单 version 须为三段数字，例如 1.0.0。"};
    if (plugin ? entry != "worker.exe" : !PackagePolicy::path(entry) || !entry.ends_with(".html"))
      return RuntimeError{"INVALID_REQUEST", plugin ? "插件入口 entry 必须是 worker.exe。" : "应用入口 entry 必须是构建目录内的相对 .html 路径。"};
    const bool local = params.value("localPublisher", false);
    if (local) {
      const auto publisher = LocalPublisher::read(dataRoot_);
      if (const auto error = std::get_if<RuntimeError>(&publisher)) return *error;
      const auto &info = std::get<Json>(publisher);
      if (info.is_null()) return RuntimeError{"PUBLISHER_REQUIRED", "请先创建本地发布者"};
      parsed["publisher"] = {{"name", info["name"]}};
    }
    const auto id = createTask("pack");
    if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED","无法分配任务标识"};
    activePreview_ = id;
    const auto job = std::make_shared<PackSelection>(PackSelection{id, local ? parsed.dump() : manifest, {}, local, params.value("interactive", false)});
    auto &task = tasks_.at(id);
    task.pack = job;
    task.info["appId"] = field(parsed["type"] == "plugin" ? "pluginId" : "appId");
    task.info["packageName"] = field("name");
    task.info["packageVersion"] = field("version");
    task.info["publisherName"] = parsed.contains("publisher") && parsed["publisher"].is_object() && parsed["publisher"].contains("name") && parsed["publisher"]["name"].is_string()
      ? parsed["publisher"]["name"].get<std::string>() : "";
    pickPackFile(browser, job);
    return Json{{"taskId", id}};
  }
  if (method == "apps.cancelInstall") {
    const auto id = params["taskId"].get<std::string>();
    const auto found = tasks_.find(id);
    if (found == tasks_.end()) return RuntimeError{"TASK_NOT_FOUND","待安装任务不存在"};
    if (found->second.info["kind"] != "install" && found->second.info["kind"] != "pluginInstall")
      return RuntimeError{"INVALID_REQUEST","任务不是安装任务"};
    if (found->second.info["status"] == "cancelled") return Json{{"taskId",id}};
    if (found->second.info["status"] != "awaitingConsent") return RuntimeError{"TASK_NOT_CANCELABLE","当前阶段不能取消，请等待任务结果"};
    found->second.preview.reset();
    found->second.info["status"] = "cancelled";
    found->second.info["phase"] = "cancelled";
    found->second.info["message"] = "已取消安装";
    activePreview_.clear();
    return Json{{"taskId",id}};
  }
  if (method == "apps.confirmInstall") {
    const auto id = params["taskId"].get<std::string>();
    const auto found = tasks_.find(id);
    if (found == tasks_.end()) return RuntimeError{"TASK_NOT_FOUND","待安装任务不存在"};
    const bool plugin = found->second.info["kind"] == "pluginInstall";
    if (found->second.info["kind"] != "install" && !plugin) return RuntimeError{"INVALID_REQUEST","任务不是安装任务"};
    if (found->second.info["status"] == "succeeded" || found->second.info["phase"] == "committing") return Json{{"taskId",id}};
    if (!found->second.preview || found->second.info["status"] != "awaitingConsent") return RuntimeError{"TASK_NOT_READY","任务不处于待确认阶段"};
    if (working_) return RuntimeError{"OPERATION_BUSY","包管理正在处理其他任务"};
    const auto appId = found->second.preview->package->id();
    if (plugin ? runtime_->pluginInUse(appId) : !runtime_->canModifyPackage(appId))
      return RuntimeError{"APP_BUSY", plugin ? "请先关闭使用此插件的应用" : "请先关闭应用，再安装或更新"};
    if (!found->second.preview->trusted && !params["trustPublisher"].get<bool>()) return RuntimeError{"PUBLISHER_UNTRUSTED","请先核对发布者指纹并明确确认信任"};
    mutate(id,plugin ? "pluginInstall" : "install",appId,params["trustPublisher"].get<bool>());
    return Json{{"taskId",id}};
  }
  if (working_ || !activePreview_.empty()) return RuntimeError{"OPERATION_BUSY","请先完成或取消当前包管理任务"};
  if (method == "apps.prepareInstall" || method == "plugins.prepareInstall") {
    const bool plugin = method == "plugins.prepareInstall";
    const auto id = createTask(plugin ? "pluginInstall" : "install");
    if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED","无法分配任务标识"};
    activePreview_ = id;
    tasks_.at(id).info["phase"] = "choosing";
    const auto title = plugin ? "选择 YPKG 插件包" : "选择 YPKG 应用包";
    tasks_.at(id).info["message"] = title;
    browser->GetHost()->RunFileDialog(FILE_DIALOG_OPEN, title, "", {".ypkg"}, new _::PackagePicker(this,alive_,id));
    return Json{{"taskId",id}};
  }
  if (method == "plugins.uninstall") {
    const auto pluginId = params["pluginId"].get<std::string>();
    if (runtime_->pluginInUse(pluginId)) return RuntimeError{"APP_BUSY","请先关闭使用此插件的应用"};
    const auto id = createTask("pluginUninstall", pluginId);
    if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED","无法分配任务标识"};
    mutate(id,"pluginUninstall",pluginId);
    return Json{{"taskId",id}};
  }
  const auto appId = params["appId"].get<std::string>();
  if (!runtime_->isInstalledPackage(appId)) return RuntimeError{"APP_NOT_INSTALLED","此应用不是可管理的安装应用"};
  if (!runtime_->canModifyPackage(appId)) return RuntimeError{"APP_BUSY","请先关闭应用，再修改应用配置"};
  const auto kind = method == "apps.clearBrowserData" ? "clearBrowserData" :
    method == "apps.clearData" ? "clearData" : method == "apps.setPermissions" ? "permissions" :
    method == "apps.uninstall" ? "uninstall" : "rollback";
  const auto id = createTask(kind,appId);
  if (id.empty()) return RuntimeError{"RESOURCE_EXHAUSTED","无法分配任务标识"};
  mutate(id,kind,appId,false,method == "apps.setPermissions" ? params["permissions"] : Json(),
    method == "apps.uninstall" && params.value("clearData", false));
  return Json{{"taskId",id}};
}
void PackageManager::pickPackFile(CefRefPtr<CefBrowser> browser, std::shared_ptr<PackSelection> job) {
  const auto step = job->paths.size();
  const CefBrowserHost::FileDialogMode modes[] = {FILE_DIALOG_OPEN_FOLDER, FILE_DIALOG_OPEN, FILE_DIALOG_SAVE};
  const char *titles[] = {"选择打包资源目录", "选择开发签名私钥", "保存 YPKG 包"};
  auto &info = tasks_.at(job->id).info;
  info["status"] = "running";
  info["phase"] = "choosing";
  info["message"] = titles[step];
  const auto manifest = PackagePolicy::json(job->manifest);
  const auto idKey = manifest["type"] == "plugin" ? "pluginId" : "appId";
  const auto name = manifest.contains(idKey) && manifest[idKey].is_string() ? manifest[idKey].get<std::string>() : "application";
  const auto version = manifest.contains("version") && manifest["version"].is_string() ? manifest["version"].get<std::string>() : "1.0.0";
  auto suggestedName = name + "-" + version + ".ypkg";
  for (auto &character : suggestedName) if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '.' || character == '-' || character == '_')) character = '_';
  const auto alive = alive_;
  browser->GetHost()->RunFileDialog(modes[step], titles[step], step == 2 ? suggestedName : "",
    step == 0 ? std::vector<CefString>{} : std::vector<CefString>{step == 1 ? ".pem" : ".ypkg"},
    new _::PackPicker([this, alive, browser, job, entry = manifest["entry"].get<std::string>()](std::filesystem::path path) {
      if (!alive->load()) return;
      if (stopping_ || path.empty()) {
        auto &info = tasks_.at(job->id).info;
        info["status"] = info["phase"] = "cancelled";
        info["message"] = "打包已取消";
        if (job->interactive && !stopping_) {
          info["status"] = "awaitingInput";
          info["phase"] = job->paths.empty() ? "source" : job->paths.size() == 1 ? "key" : "output";
          info["message"] = "未选择文件，已保留当前步骤。可重新选择或返回编辑。";
        } else activePreview_.clear();
        return;
      }
      if (job->paths.empty()) {
        if (const auto error = _::checkPackEntry(path, entry)) {
          if (job->interactive) {
            auto &info = tasks_.at(job->id).info;
            info["status"] = "awaitingInput";
            info["phase"] = "source";
            info["message"] = error->message;
          } else fail(job->id, *error);
          return;
        }
      }
      job->paths.push_back(std::move(path));
      if (job->localPublisher && job->paths.size() == 1) job->paths.push_back(LocalPublisher::path(dataRoot_));
      auto &info = tasks_.at(job->id).info;
      const auto source = job->paths[0].filename().u8string();
      info["sourceName"] = std::string(source.begin(), source.end());
      if (job->paths.size() == 3) {
        const auto output = job->paths[2].u8string();
        info["outputPath"] = std::string(output.begin(), output.end());
      }
      if (job->interactive) {
        info["status"] = "awaitingInput";
        info["phase"] = job->paths.size() == 1 ? "key" : job->paths.size() == 2 ? "output" : "ready";
        info["message"] = job->paths.size() == 3 ? "输出位置已确认，点击开始打包" : "项目已选择，请确认下一步";
      } else if (job->paths.size() == 3) buildPackage(job);
      else pickPackFile(browser, job);
    }));
}

void PackageManager::buildPackage(std::shared_ptr<PackSelection> job) {
  working_ = true;
  auto &info = tasks_.at(job->id).info;
  info["status"] = "running";
  info["phase"] = "verifying";
  info["message"] = "正在打包、签名和验签";
  const auto alive = alive_;
  worker_->GetTaskRunner()->PostTask(new FunctionTask([this, alive, job] {
    const auto manifest = dataRoot_ / ("pack-" + job->id + ".json");
    std::ofstream file(manifest, std::ios::binary | std::ios::trunc);
    file.write(job->manifest.data(), static_cast<std::streamsize>(job->manifest.size()));
    file.close();
    const auto error = file ? _::runPacker(toolsRoot_, manifest, job->paths, job->localPublisher)
      : std::optional<RuntimeError>{{"PACK_IO", "无法写入临时清单"}};
    std::error_code ignored;
    std::filesystem::remove(manifest, ignored);
    CefPostTask(TID_UI, new FunctionTask([this, alive, id = job->id, error] {
      if (!alive->load()) return;
      working_ = false;
      activePreview_.clear();
      if (error) fail(id, *error);
      else {
        auto &info = tasks_.at(id).info;
        info["status"] = "succeeded";
        info["phase"] = "committed";
        info["message"] = "验签通过，已生成 YPKG 包";
      }
      runtime_->packageWorkFinished();
    }));
  }));
}

void PackageManager::selected(const std::string &id, const std::filesystem::path &path) {
  CEF_REQUIRE_UI_THREAD();
  if (stopping_ || path.empty()) {
    tasks_.at(id).info["status"] = "cancelled";
    tasks_.at(id).info["phase"] = "cancelled";
    tasks_.at(id).info["message"] = "未选择应用包";
    activePreview_.clear();
    return;
  }
  working_ = true;
  tasks_.at(id).info["phase"] = "verifying";
  tasks_.at(id).info["message"] = "正在校验包格式、签名与资源";
  const bool plugin = tasks_.at(id).info["kind"] == "pluginInstall";
  const auto store = plugin ? pluginStore_ : store_;
  const auto alive = alive_;
  worker_->GetTaskRunner()->PostTask(new FunctionTask([this, store, alive, id, path] {
    const auto verified = PackageVerifier::read(path);
    const auto preview = std::holds_alternative<RuntimeError>(verified)
      ? RuntimeResult<PackagePreview>(std::get<RuntimeError>(verified))
      : store->preview(std::get<std::shared_ptr<const VerifiedPackage>>(verified));
    CefPostTask(TID_UI,new FunctionTask([this,alive,id,preview] {
      if (!alive->load()) return;
      working_ = false;
      if (const auto error = std::get_if<RuntimeError>(&preview)) fail(id,*error);
      else if (!stopping_) {
        auto &task = tasks_.at(id);
        task.preview = std::get<PackagePreview>(preview);
        task.info["appId"] = task.preview->package->id();
        task.info["preview"] = task.preview->json();
        task.info["appId"] = task.preview->package->id();
        task.info["status"] = "awaitingConsent";
        task.info["phase"] = "awaitingConsent";
        task.info["message"] = "校验通过，请核对应用与发布者后确认";
      }
      runtime_->packageWorkFinished();
    }));
  }));
}
void PackageManager::mutate(const std::string &id, const std::string &kind, const std::string &appId, bool trust,
    Json permissions, bool clearOnUninstall) {
  working_ = true;
  const bool plugin = kind == "pluginInstall" || kind == "pluginUninstall";
  (plugin ? lockedPlugin_ : lockedApp_) = appId;
  auto &task = tasks_.at(id);
  const bool browserCleanup = kind == "clearBrowserData";
  task.info["status"] = "running";
  task.info["phase"] = "committing";
  task.info["message"] = kind == "pluginInstall" ? "正在验证候选插件进程及握手" :
    plugin ? "正在更新插件注册表" : kind == "install" ? "正在写入并激活新版本"
    : browserCleanup ? "正在登记浏览器数据清理" : kind == "clearData" ? "正在清理应用数据" : kind == "permissions" ? "正在保存应用授权"
    : clearOnUninstall ? "正在清理数据并卸载" : kind == "uninstall" ? "正在卸载，保留应用数据" : "正在恢复上一版本";
  const auto preview = task.preview;
  const auto store = plugin ? pluginStore_ : store_;
  const auto apps = store_;
  const auto alive = alive_;
  const auto origin = browserCleanup || kind == "clearData" || clearOnUninstall ? runtime_->packageOriginKey(appId) : std::string{};
  const bool posted = worker_->GetTaskRunner()->PostTask(new FunctionTask([this,store,apps,alive,id,kind,appId,trust,preview,plugin,origin,browserCleanup,clearOnUninstall,permissions=std::move(permissions)] {
    auto error = plugin && preview ? PluginProcess::probe(dataRoot_, *preview->package) : std::nullopt;
    if (browserCleanup && !BrowserData::schedule(dataRoot_, origin))
      error = RuntimeError{"APP_BROWSER_CLEANUP_FAILED", "无法登记浏览器数据清理，请检查数据目录权限"};
    if (!error && !browserCleanup && !origin.empty()) error = _::clearAppData(dataRoot_, origin);
    const bool dataOnly = browserCleanup || kind == "clearData";
    if (!error && !dataOnly) {
      error = kind == "permissions" ? store->setPermissions(appId,id,permissions) : preview ? store->install(*preview,id,trust)
        : kind == "rollback" ? store->rollback(appId,id) : store->uninstall(appId,id);
      if (error && clearOnUninstall) error->message = "应用数据已清理，卸载失败：" + error->message;
    }
    auto listed = Json::array();
    if (!error && plugin) {
      const auto loaded = store->load();
      if (const auto failure = std::get_if<RuntimeError>(&loaded)) error = *failure;
      else listed = _::pluginList(std::get<PackageSnapshot>(loaded));
    }
    RuntimeResult<PackageSnapshot> snapshot = error ? RuntimeResult<PackageSnapshot>(*error) :
      dataOnly ? RuntimeResult<PackageSnapshot>(PackageSnapshot{}) : apps->load();
    CefPostTask(TID_UI,new FunctionTask([this,alive,id,snapshot = std::move(snapshot),listed,plugin,dataOnly,browserCleanup] {
      if (!alive->load()) return;
      working_ = false;
      lockedApp_.clear();
      lockedPlugin_.clear();
      activePreview_.clear();
      if (const auto error = std::get_if<RuntimeError>(&snapshot)) fail(id,*error);
      else {
        if (plugin) plugins_ = listed;
        if (!dataOnly) runtime_->applyPackages(std::get<PackageSnapshot>(snapshot));
        auto &task = tasks_.at(id);
        task.preview.reset();
        task.info["status"] = "succeeded";
        task.info["phase"] = "committed";
        task.info["message"] = browserCleanup ? "已登记浏览器数据清理，请重启瑶光台" : "操作已完成";
        RuntimeAudit::record(dataRoot_, task.info["kind"].get<std::string>(), task.info["appId"].get<std::string>(), "",
          browserCleanup ? "PENDING_RESTART" : "OK");
      }
      runtime_->packageWorkFinished();
    }));
  }));
  if (!posted) {
    working_ = false;
    lockedApp_.clear();
    lockedPlugin_.clear();
    activePreview_.clear();
    fail(id, {"RUNTIME_STOPPING", "应用管理工作线程不可用"});
  }
}
void PackageManager::stop() { stopping_ = true; }
} // namespace alkaid
