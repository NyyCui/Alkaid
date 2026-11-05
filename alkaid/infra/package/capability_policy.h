/** @file capability_policy.h 可安装应用的能力白名单，不包含平台管理权限。 */
#ifndef ALKAID_CAPABILITY_POLICY_H
#define ALKAID_CAPABILITY_POLICY_H
#include "package_policy.h"
#include <set>
namespace alkaid::CapabilityPolicy {
inline const std::set<std::string> allowed{"storage.kv", "files.private", "files.userSelected.read", "files.userSelected.write", "net.request"};
inline constexpr std::size_t MAX_PLUGIN_DEPENDENCIES = 16;
inline bool valid(const Json &value, bool storedGrants=false, const std::set<std::string> &plugins={}) {
  if (!value.is_array() || value.size() > allowed.size() + (storedGrants ? 8 + MAX_PLUGIN_DEPENDENCIES : plugins.size())) return false;
  std::set<std::string> seen;
  for (const auto &item : value) {
    if (!item.is_string()) return false;
    const auto name = item.get<std::string>();
    const bool scope=storedGrants && name.starts_with("net.endpoint:") && name.size()==77 &&
      std::ranges::all_of(name.substr(13),[](char c){return (c>='a'&&c<='f')||(c>='0'&&c<='9');});
    const bool plugin = name.starts_with("plugin.") &&
      (storedGrants ? PackagePolicy::appId(name.substr(7)) : plugins.contains(name.substr(7)));
    if ((!allowed.contains(name) && !scope && !plugin) || !seen.insert(name).second) return false;
  }
  return true;
}
}
#endif // ALKAID_CAPABILITY_POLICY_H
