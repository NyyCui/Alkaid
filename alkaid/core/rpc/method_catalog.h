/** @file method_catalog.h 由 scripts/generate-contracts.py 生成，请修改契约源文件。 */
#ifndef ALKAID_METHOD_CATALOG_H
#define ALKAID_METHOD_CATALOG_H
#include <array>
#include <cstddef>
#include <string_view>
namespace alkaid {
struct MethodContract {
  std::string_view name;
  std::string_view capability;
  bool managerOnly;
  int timeoutMs;
};
inline constexpr int PROTOCOL_VERSION = 1;
inline constexpr std::size_t MAX_PAYLOAD_BYTES = 1048576;
inline constexpr std::size_t MAX_PENDING_PER_SESSION = 128;
inline constexpr std::array<MethodContract, 69> METHOD_CATALOG = {{
  {"runtime.getInfo", "", false, 15000},
  {"runtime.getCapabilities", "", false, 15000},
  {"apps.list", "apps.manage", true, 15000},
  {"apps.launch", "apps.manage", true, 15000},
  {"apps.focus", "apps.manage", true, 15000},
  {"apps.close", "apps.manage", true, 15000},
  {"plugins.list", "apps.manage", true, 15000},
  {"plugins.prepareInstall", "apps.manage", true, 15000},
  {"plugins.uninstall", "apps.manage", true, 15000},
  {"apps.publisherBackup", "apps.manage", true, 15000},
  {"apps.localPublisher", "apps.manage", true, 15000},
  {"apps.preparePack", "apps.manage", true, 15000},
  {"apps.prepareInstall", "apps.manage", true, 15000},
  {"apps.confirmInstall", "apps.manage", true, 15000},
  {"apps.cancelInstall", "apps.manage", true, 15000},
  {"apps.getTask", "apps.manage", true, 15000},
  {"apps.listAudit", "apps.manage", true, 15000},
  {"apps.listTasks", "apps.manage", true, 15000},
  {"apps.setPermissions", "apps.manage", true, 15000},
  {"apps.clearData", "apps.manage", true, 15000},
  {"apps.clearBrowserData", "apps.manage", true, 15000},
  {"apps.uninstall", "apps.manage", true, 15000},
  {"apps.rollback", "apps.manage", true, 15000},
  {"runtime.exit", "platform.exit", true, 15000},
  {"plugin.call", "", false, 30000},
  {"events.subscribe", "", false, 15000},
  {"events.unsubscribe", "", false, 15000},
  {"config.app.get", "", false, 15000},
  {"config.app.set", "", false, 15000},
  {"config.app.remove", "", false, 15000},
  {"config.app.batch", "", false, 15000},
  {"config.platform.get", "apps.manage", true, 15000},
  {"config.platform.set", "apps.manage", true, 15000},
  {"config.platform.remove", "apps.manage", true, 15000},
  {"config.platform.batch", "apps.manage", true, 15000},
  {"storage.get", "storage.kv", false, 15000},
  {"storage.set", "storage.kv", false, 15000},
  {"storage.remove", "storage.kv", false, 15000},
  {"storage.batch", "storage.kv", false, 15000},
  {"files.readPrivateText", "files.private", false, 15000},
  {"files.writePrivateText", "files.private", false, 15000},
  {"files.removePrivate", "files.private", false, 15000},
  {"dialog.openFile", "files.userSelected.read", false, 300000},
  {"dialog.saveFile", "files.userSelected.write", false, 300000},
  {"files.readText", "files.userSelected.read", false, 15000},
  {"files.writeText", "files.userSelected.write", false, 15000},
  {"files.close", "", false, 15000},
  {"files.beginRead", "files.userSelected.read", false, 15000},
  {"files.beginWrite", "files.userSelected.write", false, 15000},
  {"files.beginPrivateRead", "files.private", false, 15000},
  {"files.beginPrivateWrite", "files.private", false, 15000},
  {"files.readNext", "", false, 15000},
  {"files.appendChunk", "", false, 15000},
  {"files.commitWrite", "", false, 15000},
  {"files.abortStream", "", false, 15000},
  {"window.setCloseHandler", "", false, 15000},
  {"window.resolveClose", "", false, 15000},
  {"net.getEndpoints", "net.request", false, 15000},
  {"net.open", "net.request", false, 15000},
  {"net.readNext", "", false, 15000},
  {"net.close", "", false, 15000},
  {"window.minimize", "apps.manage", true, 15000},
  {"workspace.files", "workspace.files", false, 15000},
  {"apps.layoutWorkspace", "apps.manage", true, 15000},
  {"window.toggleMaximize", "apps.manage", true, 15000},
  {"apps.advancePack", "apps.manage", true, 15000},
  {"apps.cancelPack", "apps.manage", true, 15000},
  {"apps.revealPack", "apps.manage", true, 15000},
  {"apps.installPacked", "apps.manage", true, 15000},
}};
} // namespace alkaid
#endif // ALKAID_METHOD_CATALOG_H
