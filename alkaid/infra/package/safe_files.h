/** @file safe_files.h 安装目录禁止重解析点，持久化文件完成后才允许激活。 */
#ifndef ALKAID_PACKAGE_SAFE_FILES_H
#define ALKAID_PACKAGE_SAFE_FILES_H
#include "package_types.h"
#include <windows.h>

namespace alkaid::PackageFiles {
inline bool safe(const std::filesystem::path &path) {
  auto current = path.root_path();
  for (const auto &part : path.relative_path()) {
    current /= part;
    const auto attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
  }
  return true;
}
inline bool directory(const std::filesystem::path &path) {
  if (!safe(path)) return false;
  std::error_code error;
  std::filesystem::create_directories(path, error);
  return !error && safe(path);
}
inline bool write(const std::filesystem::path &path, const std::string &bytes) {
  if (!safe(path)) return false;
  const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
    written == bytes.size() && FlushFileBuffers(handle);
  CloseHandle(handle);
  return ok;
}
inline bool removeTree(const std::filesystem::path &root, const std::filesystem::path &path) {
  const auto relative = path.lexically_relative(root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || path == root || !safe(path)) return false;
  std::error_code error;
  std::filesystem::remove_all(path, error);
  return !error;
}
} // namespace alkaid::PackageFiles
#endif // ALKAID_PACKAGE_SAFE_FILES_H
