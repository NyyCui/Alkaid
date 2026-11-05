/** @file plugin_process.cpp 校验绑定包，启动独立进程并执行本机管道握手。 */
#include "plugin_process.h"
#include "runtime_audit.h"
#include "package_verifier.h"
#include "package_policy.h"
#include "safe_files.h"
#include "crypto.h"
#include "function_task.h"
#include <sddl.h>

namespace alkaid {
namespace {
constexpr DWORD MAX_FRAME = 256 * 1024;
RuntimeError unavailable() { return {"PLUGIN_UNAVAILABLE", "插件进程不可用或协议响应不合法"}; }
std::string randomId() {
  std::array<unsigned char, 24> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
  return PackageCrypto::hex(bytes);
}
PSECURITY_DESCRIPTOR userPipeSecurity() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> bytes(size);
  const bool read = size && GetTokenInformation(token, TokenUser, bytes.data(), size, &size);
  CloseHandle(token);
  if (!read) return nullptr;
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &sid)) return nullptr;
  const std::wstring acl = std::wstring(L"D:P(A;;GA;;;") + sid + L")S:(ML;;NW;;;LW)";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR security = nullptr;
  ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &security, nullptr);
  return security;
}
HANDLE restrictedPluginToken() {
  HANDLE primary = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
      &primary)) return nullptr;
  DWORD size = 0;
  GetTokenInformation(primary, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> user(size);
  BYTE adminSid[SECURITY_MAX_SID_SIZE]{};
  DWORD adminSize = sizeof(adminSid);
  HANDLE restricted = nullptr;
  if (size && GetTokenInformation(primary, TokenUser, user.data(), size, &size) &&
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &adminSize)) {
    SID_AND_ATTRIBUTES disabled{adminSid, 0};
    SID_AND_ATTRIBUTES allowed{reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, 0};
    CreateRestrictedToken(primary, DISABLE_MAX_PRIVILEGE | WRITE_RESTRICTED,
      1, &disabled, 0, nullptr, 1, &allowed, &restricted);
  }
  CloseHandle(primary);
  if (!restricted) return nullptr;
  PSID low = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-16-4096", &low)) { CloseHandle(restricted); return nullptr; }
  TOKEN_MANDATORY_LABEL integrity{};
  integrity.Label.Sid = low;
  integrity.Label.Attributes = SE_GROUP_INTEGRITY;
  const bool lowered = SetTokenInformation(restricted, TokenIntegrityLevel, &integrity,
    sizeof(integrity) + GetLengthSid(low));
  LocalFree(low);
  if (!lowered) { CloseHandle(restricted); return nullptr; }
  return restricted;
}
}

struct PluginProcess::Instance {
  std::filesystem::path root, directory;
  HANDLE pipe = INVALID_HANDLE_VALUE, process = nullptr, job = nullptr, executable = INVALID_HANDLE_VALUE;
  Json methods, events;
  std::string launchError;
  std::function<void(std::string, Json)> eventSink;
  ~Instance() {
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    if (job) CloseHandle(job);
    if (process) { WaitForSingleObject(process, 1000); CloseHandle(process); }
    if (executable != INVALID_HANDLE_VALUE) CloseHandle(executable);
    if (!directory.empty()) PackageFiles::removeTree(root, directory);
  }
  bool finishIo(OVERLAPPED &operation, const DataLease &lease, DWORD &bytes,
      const std::function<bool()> &poll = {}) const {
    while (lease.valid() && (!poll || poll())) {
      const HANDLE handles[]{operation.hEvent, process};
      const auto status = WaitForMultipleObjects(2, handles, FALSE, 25);
      if (status == WAIT_OBJECT_0) return GetOverlappedResult(pipe, &operation, &bytes, FALSE) != FALSE;
      if (status != WAIT_TIMEOUT) break;
    }
    CancelIoEx(pipe, &operation);
    GetOverlappedResult(pipe, &operation, &bytes, TRUE);
    return false;
  }
  bool transfer(bool writing, void *buffer, DWORD size, const DataLease &lease,
      const std::function<bool()> &poll = {}) const {
    auto *data = static_cast<char *>(buffer);
    while (size && lease.valid() && (!poll || poll())) {
      OVERLAPPED operation{};
      operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!operation.hEvent) return false;
      DWORD count = 0;
      const bool started = writing ? WriteFile(pipe, data, size, &count, &operation)
                                   : ReadFile(pipe, data, size, &count, &operation);
      const bool ok = started || (GetLastError() == ERROR_IO_PENDING && finishIo(operation, lease, count, poll));
      CloseHandle(operation.hEvent);
      if (!ok || !count || count > size) return false;
      size -= count;
      data += count;
    }
    return size == 0;
  }
  bool send(const Json &message, const DataLease &lease) const {
    auto text = message.dump();
    if (text.empty() || text.size() > MAX_FRAME) return false;
    auto length = static_cast<DWORD>(text.size());
    return transfer(true, &length, sizeof(length), lease) && transfer(true, text.data(), length, lease);
  }
  Json receive(const DataLease &lease, const std::function<bool()> &poll = {}) const {
    DWORD length = 0;
    if (!transfer(false, &length, sizeof(length), lease, poll) || !length || length > MAX_FRAME) return {};
    std::string text(length, '\0');
    return transfer(false, text.data(), length, lease, poll) ? PackagePolicy::json(text) : Json();
  }
  bool forwardEvent(Json frame, uint64_t session, bool deliver) const {
    if (!PackagePolicy::keys(frame, {"type","owner","event","payload"}) || frame["type"] != "event" ||
        frame["owner"] != std::to_string(session) || !frame["event"].is_string() ||
        std::find(events.begin(), events.end(), frame["event"]) == events.end()) return false;
    if (deliver) eventSink(frame["event"].get<std::string>(), std::move(frame["payload"]));
    return true;
  }
  bool drainIdle(uint64_t session) {
    // 同一工作线程负责空闲与调用中的读操作。开始读取后用独立期限收齐帧，允许帧大于管道缓冲区。
    for (int index = 0; index < 4; ++index) {
      DWORD available = 0;
      if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
      if (available == 0) return true;
      DataLease lease;
      lease.deadline = RuntimeClock::now() + std::chrono::milliseconds(150);
      if (!forwardEvent(receive(lease), session, true)) return false;
    }
    return true;
  }
  bool launch(const VerifiedPackage &package, const DataLease &lease) {
    const auto failure = [this](const char *stage) {
      const auto code = GetLastError();
      launchError = std::string(stage) + "（Win32 " + std::to_string(code) + "）";
      return false;
    };
    const auto nonce = randomId();
    if (nonce.empty()) return false;
    const auto location = root / nonce;
    std::error_code error;
    if (!PackageFiles::directory(root) || !std::filesystem::create_directory(location, error) || error) return false;
    directory = location;
    const auto worker = directory / "worker.exe";
    if (!PackageFiles::write(worker, package.assets.at("worker.exe"))) return false;
    executable = CreateFileW(worker.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (executable == INVALID_HANDLE_VALUE) return false;
    const auto pipeName = L"\\\\.\\pipe\\alkaid-plugin-" + std::wstring(nonce.begin(), nonce.end());
    const auto descriptor = userPipeSecurity();
    if (!descriptor) return failure("无法生成插件管道权限");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return failure("无法创建插件管道");
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return false;
    auto command = L"\"" + worker.wstring() + L"\" " + pipeName;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const auto token = restrictedPluginToken();
    if (!token) return failure("无法创建受限插件令牌");
    const bool created = CreateProcessAsUserW(token, worker.c_str(), command.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &child);
    const auto createError = GetLastError();
    CloseHandle(token);
    if (!created) { SetLastError(createError); return failure("无法启动受限插件进程"); }
    process = child.hProcess;
    const bool started = AssignProcessToJobObject(job, process) && ResumeThread(child.hThread) != DWORD(-1);
    CloseHandle(child.hThread);
    if (!started) { TerminateProcess(process, 1); return false; }
    OVERLAPPED connection{};
    connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connection.hEvent) return false;
    DWORD ignored = 0;
    const bool connected = ConnectNamedPipe(pipe, &connection) || GetLastError() == ERROR_PIPE_CONNECTED ||
      (GetLastError() == ERROR_IO_PENDING && finishIo(connection, lease, ignored));
    CloseHandle(connection.hEvent);
    ULONG peer = 0;
    if (!connected || !GetNamedPipeClientProcessId(pipe, &peer) || peer != child.dwProcessId) {
      DWORD exitCode = 0;
      GetExitCodeProcess(process, &exitCode);
      launchError = "插件管道握手连接失败（进程退出码 " + std::to_string(exitCode) + "）";
      return false;
    }
    methods = package.pluginCapabilities;
    events = package.pluginEvents;
    if (!send({{"type","hello"},{"protocolMajor",1},{"pluginId",package.pluginId},{"version",package.version}}, lease)) return false;
    const auto ready = receive(lease);
    return PackagePolicy::keys(ready, {"type","protocolMajor","pluginId","version","methods","events"}) &&
      ready["type"] == "ready" && ready["protocolMajor"] == 1 && ready["pluginId"] == package.pluginId &&
      ready["version"] == package.version && ready["methods"] == methods && ready["events"] == events;
  }
};

PluginProcess::~PluginProcess() = default;
std::optional<RuntimeError> PluginProcess::probe(const std::filesystem::path &dataRoot, const VerifiedPackage &package) {
  Instance candidate;
  candidate.root = dataRoot / "plugin-work";
  DataLease handshake;
  handshake.deadline = RuntimeClock::now() + std::chrono::seconds(5);
  if (!candidate.launch(package, handshake))
    return RuntimeError{"PLUGIN_HANDSHAKE_FAILED", candidate.launchError.empty() ?
      "候选插件启动或握手失败，当前版本未变更" : candidate.launchError + "，当前版本未变更"};
  DataLease shutdown;
  shutdown.deadline = RuntimeClock::now() + std::chrono::milliseconds(250);
  if (candidate.send({{"type","shutdown"}}, shutdown)) WaitForSingleObject(candidate.process, 250);
  return std::nullopt;
}

void PluginProcess::startPolling(CefRefPtr<CefTaskRunner> runner) {
  if (pollScheduled_ || instances_.empty()) return;
  pollScheduled_ = runner->PostDelayedTask(new FunctionTask([weak = weak_from_this(), runner] {
    if (const auto process = weak.lock()) {
      process->pollScheduled_ = false;
      process->pollIdle();
      process->startPolling(runner);
    }
  }), 50);
}

void PluginProcess::pollIdle() {
  std::erase_if(instances_, [&](const auto &entry) {
    if (entry.second->drainIdle(entry.first.first)) return false;
    RuntimeAudit::record(dataRoot_, "pluginExit", appId_, entry.first.second, "PLUGIN_UNAVAILABLE");
    return true;
  });
}

void PluginProcess::release(uint64_t session) {
  for (auto entry = instances_.begin(); entry != instances_.end();) {
    if (entry->first.first != session) { ++entry; continue; }
    DataLease cleanup;
    cleanup.deadline = RuntimeClock::now() + std::chrono::milliseconds(250);
    const auto &instance = entry->second;
    if (instance->send({{"type","releaseOwner"},{"owner",std::to_string(session)}}, cleanup) &&
        instance->send({{"type","shutdown"}}, cleanup))
      WaitForSingleObject(instance->process, 250);
    entry = instances_.erase(entry);
  }
}
RuntimeResult<Json> PluginProcess::call(const std::string &id, const PluginBinding &binding, const std::string &method,
    const Json &params, const CallPermit &permit, const DataLease &lease,
    const std::function<void(std::string, Json)> &event) {
  if (!lease.valid()) return RuntimeError{"TIMEOUT", "插件调用已取消或超时"};
  const auto key = std::make_pair(permit.sessionId, id);
  auto entry = instances_.find(key);
  if (entry == instances_.end()) {
    if (instances_.size() >= 4) return RuntimeError{"RESOURCE_EXHAUSTED", "当前窗口插件进程数量已达上限"};
    const auto path = dataRoot_ / "plugins" / "versions" / binding.digest / "package.ypkg";
    if (!PackageFiles::safe(path)) return unavailable();
    const auto verified = PackageVerifier::read(path);
    const auto package = std::get_if<std::shared_ptr<const VerifiedPackage>>(&verified);
    if (!package || (*package)->digest != binding.digest || (*package)->pluginId != id ||
        (*package)->version != binding.version || (*package)->publisherKeyId != binding.publisherKeyId) return unavailable();
    const auto &methods = (*package)->pluginCapabilities;
    if (std::find(methods.begin(), methods.end(), Json(method)) == methods.end())
      return RuntimeError{"METHOD_NOT_FOUND", "插件方法未在签名清单中声明"};
    auto instance = std::make_shared<Instance>();
    instance->root = dataRoot_ / "plugin-work";
    instance->eventSink = event;
    DataLease handshake;
    handshake.deadline = std::min(lease.deadline, RuntimeClock::now() + std::chrono::seconds(5));
    if (!lease.valid() || !instance->launch(**package, handshake))
      return instance->launchError.empty() ? unavailable() : RuntimeError{"PLUGIN_UNAVAILABLE", instance->launchError};
    entry = instances_.emplace(key, std::move(instance)).first;
  }
  const auto &instance = entry->second;
  if (std::find(instance->methods.begin(), instance->methods.end(), Json(method)) == instance->methods.end())
    return RuntimeError{"METHOD_NOT_FOUND", "插件方法未在签名清单中声明"};
  const auto request = std::to_string(permit.ticket);
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(lease.deadline - RuntimeClock::now()).count();
  const auto sent = remaining > 0 && instance->send({{"type","call"},{"id",request},{"method",method},
    {"params",params},{"owner",std::to_string(permit.sessionId)},{"timeoutMs",remaining}}, lease);
  constexpr auto cancelGrace = std::chrono::milliseconds(300);
  DataLease readLease;
  readLease.deadline = lease.deadline + cancelGrace;
  bool cancelling = false;
  const auto poll = [&] {
    if (lease.valid() || cancelling) return true;
    cancelling = true;
    readLease.deadline = RuntimeClock::now() + cancelGrace;
    // 保留原来的读取操作，让取消确认接在同一条帧流上。
    return instance->send({{"type","cancel"},{"id",request}}, readLease);
  };
  auto reply = sent ? instance->receive(readLease, poll) : Json();
  std::size_t eventCount = 0;
  while (reply.is_object() && reply.value("type", Json()) == "event") {
    if (++eventCount > 32 || !instance->forwardEvent(std::move(reply), permit.sessionId, lease.valid())) {
      reply = Json();
      break;
    }
    reply = instance->receive(readLease, poll);
  }
  const bool valueReply = PackagePolicy::keys(reply, {"type","id","value"}) &&
    reply["type"] == "result" && reply["id"] == request;
  const bool errorReply = PackagePolicy::keys(reply, {"type","id","error"}) && reply["type"] == "result" &&
    reply["id"] == request && PackagePolicy::keys(reply["error"], {"code","message"}) &&
    reply["error"]["code"].is_string() && reply["error"]["message"].is_string();
  if (lease.valid() && valueReply)
    return reply["value"];
  if (lease.valid() && errorReply)
    return RuntimeError{reply["error"]["code"].get<std::string>(), reply["error"]["message"].get<std::string>()};
  if (!(cancelling && errorReply && reply["error"]["code"] == "ABORTED")) instances_.erase(entry);
  return lease.valid() ? unavailable() : RuntimeError{"TIMEOUT", "插件调用已取消或超时"};
}
}
