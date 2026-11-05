/** @file browser_data.h 在 CEF 重新启动且应用上下文尚未创建时清理独立配置目录。 */
#ifndef ALKAID_BROWSER_DATA_H
#define ALKAID_BROWSER_DATA_H
#include "safe_files.h"
#include <algorithm>
#include <set>
#include <string_view>

namespace alkaid::BrowserData {
inline auto queue(const std::filesystem::path &root) { return root / "config" / "browser-cleanup"; }

inline bool validOrigin(std::wstring_view origin) {
  return origin.size() == 34 && origin.starts_with(L"a-") &&
    std::ranges::all_of(origin.substr(2), [](wchar_t c) {
      return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
    });
}

inline bool pending(const std::filesystem::path &root, const std::string &origin) {
  std::error_code error;
  const bool exists = std::filesystem::exists(queue(root) / origin, error);
  return exists || bool(error);
}

inline bool schedule(const std::filesystem::path &root, const std::string &origin) {
  const std::filesystem::path key(origin);
  if (!validOrigin(key.native()) || !PackageFiles::safe(root / "profiles" / key)) return false;
  const auto marker = queue(root) / origin;
  std::error_code error;
  return PackageFiles::directory(queue(root)) &&
    PackageFiles::safe(marker) && (std::filesystem::is_regular_file(marker, error) ||
      PackageFiles::write(marker, ""));
}

inline std::set<std::string> applyPending(const std::filesystem::path &root) {
  std::set<std::string> failed;
  const auto directory = queue(root);
  if (!PackageFiles::safe(directory)) return failed;
  const auto profiles = root / "profiles";
  std::error_code error;
  for (auto it = std::filesystem::directory_iterator(directory, error);
       !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
    const auto marker = it->path();
    std::error_code itemError;
    if (!validOrigin(marker.filename().native())) continue;
    if (!PackageFiles::safe(marker) || !it->is_regular_file(itemError) ||
        !PackageFiles::removeTree(profiles, profiles / marker.filename())) {
      failed.insert(marker.filename().string());
      continue;
    }
    std::filesystem::remove(marker, itemError);
    if (itemError) failed.insert(marker.filename().string());
  }
  return failed;
}
}
#endif
