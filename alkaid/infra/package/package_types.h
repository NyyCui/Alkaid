/** @file package_types.h 包交付的数据类型，不依赖 CEF。 */
#ifndef ALKAID_PACKAGE_TYPES_H
#define ALKAID_PACKAGE_TYPES_H
#include "runtime/runtime_types.h"
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include <set>
#include <nlohmann/json.hpp>

namespace alkaid {
using Json = nlohmann::json;
struct WindowOptions {
  int width = 1180, height = 780, minWidth = 760, minHeight = 560;
  bool resizable = true;
};
struct VerifiedPackage {
  std::string appId, name, version, publisherKeyId, publicKey, digest;
  std::string entry, publisherName, icon;
  std::string pluginId;
  Json pluginCapabilities = Json::array(), pluginEvents = Json::array();
  WindowOptions window;
  std::map<std::string, std::string> assets;
  std::string archive;
  std::set<std::string> permissions;
  Json networkEndpoints = Json::array();
  Json dependencies = Json::array();
  const std::string &id() const { return pluginId.empty() ? appId : pluginId; }
};
struct PackagePreview {
  std::shared_ptr<const VerifiedPackage> package;
  bool trusted = false;
  std::string currentVersion;
  std::set<std::string> addedPermissions;
  Json json() const {
    if (!package->pluginId.empty())
      return {{"type", "plugin"}, {"pluginId", package->pluginId}, {"name", package->name}, {"version", package->version},
        {"publisherKeyId", package->publisherKeyId}, {"publisherName", package->publisherName},
        {"protocolMajor", 1}, {"architecture", "win-x64"}, {"entry", package->entry},
        {"capabilities", package->pluginCapabilities}, {"events", package->pluginEvents},
        {"publisherTrusted", trusted}, {"currentVersion", currentVersion},
        {"fileCount", package->assets.size()}, {"packageBytes", package->archive.size()}};
    return {{"appId", package->appId}, {"name", package->name}, {"version", package->version},
      {"publisherKeyId", package->publisherKeyId}, {"publisherName", package->publisherName}, {"publisherTrusted", trusted},
      {"currentVersion", currentVersion}, {"permissions", package->permissions}, {"addedPermissions", addedPermissions},
      {"networkEndpoints", package->networkEndpoints}, {"dependencies", package->dependencies},
      {"fileCount", package->assets.size()}, {"packageBytes", package->archive.size()}};
  }
};
struct PluginBinding {
  std::string version, digest, publisherKeyId;
  std::set<std::string> events;
};
struct InstalledPackage {
  std::string appId, name, version, publisherKeyId, originKey, error;
  bool canRollback = false;
  std::shared_ptr<const VerifiedPackage> package;
  std::set<std::string> grants;
  std::map<std::string, PluginBinding> pluginBindings;
  std::optional<RuntimeError> dependencyError;
};
using PackageSnapshot = std::vector<InstalledPackage>;
} // namespace alkaid
#endif // ALKAID_PACKAGE_TYPES_H
