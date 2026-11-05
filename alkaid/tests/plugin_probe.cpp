/** @file plugin_probe.cpp 有真实管道往返与故障注入的签名测试工作进程。 */
#include "plugin_wire.h"

namespace {
using alkaid::Json;
using alkaid::PluginWire::read;
using alkaid::PluginWire::write;
Json tokenInfo() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return Json::object();
  DWORD size = 0;
  GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
  std::vector<unsigned char> bytes(size);
  DWORD integrity = 0;
  if (size && GetTokenInformation(token, TokenIntegrityLevel, bytes.data(), size, &size)) {
    const auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(bytes.data())->Label.Sid;
    integrity = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
  }
  const bool restricted = IsTokenRestricted(token);
  CloseHandle(token);
  const auto file = CreateFileW(L"token-probe-write.tmp", GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool writable = file != INVALID_HANDLE_VALUE;
  if (writable) { CloseHandle(file); DeleteFileW(L"token-probe-write.tmp"); }
  return {{"restricted",restricted},{"integrity",integrity},{"workspaceWritable",writable}};
}
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 2) return 2;
  const auto pipe = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  const auto hello = read(pipe);
  if (!alkaid::PackagePolicy::keys(hello, {"type","protocolMajor","pluginId","version"}) || hello["type"] != "hello")
    return 4;
  if (!write(pipe, {{"type","ready"},{"protocolMajor",1},{"pluginId",hello["pluginId"]},{"version",hello["version"]},
        {"methods",{"echo","crash","slow","badEvent","cooperative","armIdle"}},{"events",{"activity"}}})) return 5;
  unsigned cancelled = 0;
  const auto security = tokenInfo();
  while (true) {
    const auto request = read(pipe);
    if (!request.is_object()) break;
    const auto type = request.value("type", Json());
    if (type == "releaseOwner") continue;
    if (type != "call") break;
    if (request["method"] == "crash") ExitProcess(9);
    if (request["method"] == "slow") Sleep(3000);
    if (request["method"] == "cooperative") {
      const auto cancel = read(pipe);
      if (!alkaid::PackagePolicy::keys(cancel, {"type","id"}) || cancel["type"] != "cancel" || cancel["id"] != request["id"])
        break;
      ++cancelled;
      if (!write(pipe, {{"type","result"},{"id",request["id"]},
            {"error",{{"code","ABORTED"},{"message","探针已取消调用"}}}})) break;
      continue;
    }
    if (request["method"] == "echo" || request["method"] == "badEvent") {
      const auto owner = request["method"] == "badEvent" ? Json("wrong-owner") : request["owner"];
      if (!write(pipe, {{"type","event"},{"owner",owner},{"event","activity"},{"payload",request["params"]}})) break;
    }
    if (!write(pipe, {{"type","result"},{"id",request["id"]},{"value",
        {{"params",request["params"]},{"pid",GetCurrentProcessId()},{"version",hello["version"]},{"cancelled",cancelled},
         {"security",security}}}})) break;
    if (request["method"] == "armIdle") {
      Sleep(250);
      if (request["params"] == "exit") ExitProcess(9);
      if (!write(pipe, {{"type","event"},{"owner",request["owner"]},{"event","activity"},
            {"payload",{{"idle",true},{"bytes",std::string(80 * 1024,'x')}}}})) break;
    }
  }
  CloseHandle(pipe);
  return 0;
}
