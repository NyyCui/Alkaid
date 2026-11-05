/** @file package_tool.cpp 独立包校验工具与隔离注册表故障验证入口。 */
#include "package_store.h"
#include "crypto.h"
#include <iostream>

namespace {
int fail(const alkaid::RuntimeError &error) {
  std::cout << alkaid::Json{{"error",{{"code",error.code},{"message",error.message}}}}.dump() << '\n';
  return 1;
}
std::string utf8(const wchar_t *value) {
  const std::wstring text(value);
  const auto size = WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
  std::string result(size,'\0');
  WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
  return result;
}
}
int wmain(int argc, wchar_t **argv) {
  if (argc < 3) return fail({"USAGE", "用法：verify 包路径，或 list/inspect/install/uninstall/rollback 数据目录 [包路径或应用标识] [trust] [故障阶段]"});
  const auto command = utf8(argv[1]);
  if (command == "verify") {
    const auto result = alkaid::PackageVerifier::read(argv[2]);
    if (const auto error = std::get_if<alkaid::RuntimeError>(&result)) return fail(*error);
    const auto package = std::get<std::shared_ptr<const alkaid::VerifiedPackage>>(result);
    std::cout << alkaid::PackagePreview{package,false,""}.json().dump() << '\n';
    return 0;
  }
  const bool plugin = command == "plugin-install" || command == "plugin-list" ||
    command == "plugin-inspect" || command == "plugin-resolve" || command == "plugin-uninstall";
  const auto fault = argc > 5 && command != "plugin-resolve" ? utf8(argv[5]) : "";
  auto directory = std::filesystem::absolute(argv[2]);
  if (plugin) directory /= "plugins";
  alkaid::PackageStore store(directory, [fault](const std::string &stage) { if (stage == fault) ExitProcess(77); },
    plugin ? alkaid::PackageStore::Kind::Plugin : alkaid::PackageStore::Kind::Application);
  if (const auto error = store.initialize()) return fail(*error);
  auto snapshot = store.load(true);
  if (const auto error = std::get_if<alkaid::RuntimeError>(&snapshot)) return fail(*error);
  const auto id = alkaid::PackageCrypto::sha256(std::to_string(GetCurrentProcessId()) + std::to_string(GetTickCount64()));
  if (command == "plugin-resolve") {
    if (argc != 6) return fail({"USAGE", "用法：plugin-resolve 数据目录 插件ID 最低版本 排除的最高版本"});
    const auto result = store.resolvePlugin(utf8(argv[3]), utf8(argv[4]), utf8(argv[5]));
    if (const auto error = std::get_if<alkaid::RuntimeError>(&result)) return fail(*error);
    std::cout << alkaid::PackagePreview{std::get<std::shared_ptr<const alkaid::VerifiedPackage>>(result),true,""}.json().dump() << '\n';
    return 0;
  }
  if (command == "install" || command == "inspect" || command == "plugin-install" || command == "plugin-inspect") {
    if (argc < 4) return fail({"USAGE","缺少包路径"});
    const auto verified = alkaid::PackageVerifier::read(argv[3]);
    if (const auto error = std::get_if<alkaid::RuntimeError>(&verified)) return fail(*error);
    const auto preview = store.preview(std::get<std::shared_ptr<const alkaid::VerifiedPackage>>(verified));
    if (const auto error = std::get_if<alkaid::RuntimeError>(&preview)) return fail(*error);
    if (command == "inspect" || command == "plugin-inspect") { std::cout << std::get<alkaid::PackagePreview>(preview).json().dump() << '\n'; return 0; }
    if (const auto error = store.install(std::get<alkaid::PackagePreview>(preview), id, argc > 4 && utf8(argv[4]) == "trust")) return fail(*error);
  } else if (command == "uninstall" || command == "rollback" || command == "plugin-uninstall") {
    if (argc < 4) return fail({"USAGE","缺少应用标识"});
    const auto error = command == "rollback" ? store.rollback(utf8(argv[3]),id) : store.uninstall(utf8(argv[3]),id);
    if (error) return fail(*error);
  } else if (command != "list" && command != "plugin-list") return fail({"USAGE","未知命令"});
  snapshot = store.load(true);
  if (const auto error = std::get_if<alkaid::RuntimeError>(&snapshot)) return fail(*error);
  auto apps = alkaid::Json::array();
  if (plugin) {
    for (const auto &entry : std::get<alkaid::PackageSnapshot>(snapshot))
      apps.push_back({{"pluginId",entry.appId},{"version",entry.version},{"error",entry.error},{"publisherKeyId",entry.publisherKeyId}});
    std::cout << alkaid::Json{{"plugins",apps}}.dump() << '\n';
    return 0;
  }
  for (const auto &app : std::get<alkaid::PackageSnapshot>(snapshot)) apps.push_back({{"appId",app.appId},{"version",app.version},{"originKey",app.originKey},{"error",app.error},{"canRollback",app.canRollback},{"grants",app.grants}});
  std::cout << alkaid::Json{{"apps",apps},{"history",store.history()}}.dump() << '\n';
  return 0;
}
