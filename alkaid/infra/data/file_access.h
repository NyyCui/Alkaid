/** @file file_access.h Windows 句柄与目录固定：应用不能传入或获得系统路径。 */
#ifndef ALKAID_FILE_ACCESS_H
#define ALKAID_FILE_ACCESS_H
#include "crypto.h"
#include <filesystem>
#include <utility>
namespace alkaid::FileAccess {
class Handle {
public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  Handle &operator=(Handle &&other) noexcept { if (this != &other) { reset(); value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE); } return *this; }
  HANDLE get() const { return value_; }
  explicit operator bool() const { return value_ != INVALID_HANDLE_VALUE; }
  void reset() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value_, INVALID_HANDLE_VALUE)); }
private:
  HANDLE value_;
};
inline std::string randomId() {
  std::array<unsigned char, 32> bytes{};
  return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ? "" : PackageCrypto::hex(bytes);
}
inline bool utf8(const std::string &text) {
  return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}
inline bool regular(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  return GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
    !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks == 1;
}
inline bool same(HANDLE left, HANDLE right) {
  BY_HANDLE_FILE_INFORMATION a{}, b{};
  return GetFileInformationByHandle(left, &a) && GetFileInformationByHandle(right, &b) &&
    a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
/** 仅接受本地固定磁盘；逐层以不共享删除的句柄固定目录并拒绝重解析点。 */
inline bool pin(const std::filesystem::path &directory, std::vector<Handle> &pins) {
  const auto root = directory.root_path();
  const auto drive = root.wstring();
  if (!directory.is_absolute() || drive.size() != 3 || drive[1] != L':' || GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) return false;
  auto current = root;
  const auto open = [&](const std::filesystem::path &path) {
    Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle || !GetFileInformationByHandle(handle.get(), &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    pins.push_back(std::move(handle)); return true;
  };
  if (!open(current)) return false;
  for (const auto &part : directory.relative_path()) {
    if (part == L"." || part == L".." || part.empty()) return false;
    current /= part;
    if (!open(current)) return false;
  }
  return true;
}
inline Handle open(const std::filesystem::path &path) {
  return Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
}
}
#endif // ALKAID_FILE_ACCESS_H
