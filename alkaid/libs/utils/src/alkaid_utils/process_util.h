#ifndef ALKAID_CTRL_PROCESS_UTIL_H
#define ALKAID_CTRL_PROCESS_UTIL_H
#include <windows.h>

#include <filesystem>
#include <string>

namespace alkaid::ProcessUtil {
inline std::filesystem::path homeDir() {
  static std::filesystem::path homeDir = [] {
    const std::string &path = HOME_PATH;
    if (!path.empty()) return std::filesystem::path(path);
    wchar_t buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    const auto exePath = std::filesystem::path(buffer);
    const auto dir = exePath.parent_path();
    if (dir.string().ends_with("bin")) {
      return dir.parent_path();
    }
    return dir;
  }();
  return homeDir;
}

} // namespace alkaid::ProcessUtil

#endif // ALKAID_CTRL_PROCESS_UTIL_H
