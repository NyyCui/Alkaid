/** @file workspace_files.h 内置文件应用只管理平台自己的工作区目录。 */
#ifndef ALKAID_WORKSPACE_FILES_H
#define ALKAID_WORKSPACE_FILES_H
#include "safe_files.h"
#include "package_policy.h"
#include "data_store.h"
#include <shellapi.h>

namespace alkaid::WorkspaceFiles {
inline constexpr char APP_ID[] = "org.alkaid.files";
inline std::optional<std::filesystem::path> relative(const std::string &text, bool single = false) {
  if (text.empty()) return single ? std::nullopt : std::optional(std::filesystem::path{});
  if (text.size() > 768 || text.starts_with('/') || text.ends_with('/') || text.find("//") != std::string::npos ||
      text.find_first_of("\\:\"<>|?*") != std::string::npos || (single && text.find('/') != std::string::npos) ||
      std::ranges::any_of(text, [](unsigned char c) { return c < 32 || c == 127; })) return std::nullopt;
  const auto path = std::filesystem::u8path(text);
  for (const auto &part : path) {
    const auto name = part.native();
    if (name.empty() || name.size() > 120 || name.back() == L'.' || name.back() == L' ') return std::nullopt;
    const auto utf8 = part.u8string();
    const auto base = PackagePolicy::lower(std::string(utf8.begin(), utf8.end()).substr(0, utf8.find(u8'.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
        (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '0' && base[3] <= '9'))
      return std::nullopt;
  }
  return path;
}
inline RuntimeResult<Json> call(const std::filesystem::path &root, const Json &params, const DataLease &lease) {
  const RuntimeError invalid{"INVALID_REQUEST", "工作区操作或文件名无效"};
  const RuntimeError io{"WORKSPACE_IO", "工作区无法访问，请检查目录权限或文件占用"};
  if (!params.is_object() || !params.contains("operation") || !params["operation"].is_string() ||
      !params.contains("path") || !params["path"].is_string()) return invalid;
  const auto operation = params["operation"].get<std::string>();
  const bool named = operation == "mkdir" || operation == "rename" || operation == "copy";
  if (params.size() != (named ? 3 : 2) || (named && (!params.contains("name") || !params["name"].is_string()))) return invalid;
  const auto path = relative(params["path"].get<std::string>());
  if (!path || ((operation == "rename" || operation == "copy") && path->empty())) return invalid;
  const auto name = named ? relative(params["name"].get<std::string>(), true) : std::optional(std::filesystem::path{});
  if (!name) return invalid;
  if (!lease.valid()) return RuntimeError{"REQUEST_EXPIRED", "工作区请求已失效"};
  if (!PackageFiles::directory(root)) return io;
  const auto target = path->empty() ? root : root / *path;
  if (!PackageFiles::safe(target)) return RuntimeError{"WORKSPACE_UNSAFE", "工作区路径包含链接"};
  std::error_code error;
  if (operation == "list") {
    auto items = Json::array();
    for (std::filesystem::directory_iterator it(target, error), end; !error && it != end; it.increment(error)) {
      const auto attributes = GetFileAttributesW(it->path().c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
      const auto utf8 = it->path().filename().u8string();
      const std::string filename(utf8.begin(), utf8.end());
      if (!relative(filename, true)) continue;
      if (items.size() == 500) return RuntimeError{"RESOURCE_EXHAUSTED", "此目录超过 500 个项目，请先在资源管理器中整理"};
      const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      const auto bytes = directory ? 0 : it->file_size(error);
      if (error) return io;
      items.push_back({{"name",filename},{"directory",directory},{"bytes",bytes}});
    }
    return error ? RuntimeResult<Json>(io) : RuntimeResult<Json>(std::move(items));
  }
  if (operation == "reveal" && path->empty()) {
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) return io;
    return Json(nullptr);
  }
  if (!named) return invalid;
  const auto destination = (operation == "mkdir" ? target : target.parent_path()) / *name;
  if (!PackageFiles::safe(destination)) return io;
  if (std::filesystem::exists(destination, error)) return RuntimeError{"WORKSPACE_EXISTS", "此名称已存在"};
  if (error) return io;
  if (operation == "mkdir") {
    if (!std::filesystem::create_directory(destination, error)) return io;
  } else if (operation == "rename") {
    if (!MoveFileW(target.c_str(), destination.c_str())) return io;
  } else {
    const auto size = std::filesystem::file_size(target, error);
    if (error) return io;
    if (size > 64 * 1024 * 1024) return RuntimeError{"FILE_TOO_LARGE", "单次复制最多 64 MiB"};
    if (!CopyFileW(target.c_str(), destination.c_str(), TRUE)) return io;
  }
  return Json(nullptr);
}
}
#endif
