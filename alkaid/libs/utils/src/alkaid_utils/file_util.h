#ifndef ALKAID_FILES_H
#define ALKAID_FILES_H

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <tl/expected.hpp>
#include <vector>

namespace alkaid::FileUtil {

/**
 * @brief 从文件流中读取指定长度的数据（从指定偏移量开始）
 * @param file 已打开的文件流（必须以 std::ios::binary 模式打开）
 * @param len 要读取的字节数，为 0 表示一直读到文件末尾，默认为 0（为 0 时，文件的大小必须可获得）
 * @param offset 偏移量，负数表示从锚点向文件头方向偏移
 * @param anchor 定位锚点 @link std::ios_base::beg @endlink @link std::ios_base::cur @endlink @link std::ios_base::end @endlink
 * @return 读取到的字节数据
 */
inline tl::expected<std::vector<uint8_t>, std::string> readFile(std::ifstream &file, size_t len = 0, const int64_t offset = 0,
                                                                const std::ios_base::seekdir anchor = std::ios_base::cur
) {
  if (!file.is_open() || file.bad()) return tl::make_unexpected("文件未打开或不可读");

  if (offset != 0) {
    file.seekg(offset, anchor);
    if (file.fail()) return tl::make_unexpected("文件定位失败，无法读取文件");
  }

  if (len == 0) {
    const auto pos = file.tellg();

    file.seekg(0, std::ios::end);
    if (file.fail()) return tl::make_unexpected("文件定位失败，无法获取文件大小");
    len = file.tellg() - pos;
    if (len == -1) return tl::make_unexpected("无法获取文件大小（可能不是普通文件）");

    file.seekg(pos, std::ios::beg);
    if (file.fail()) return tl::make_unexpected("文件定位失败，无法读取文件");

    if (len == 0) return std::vector<uint8_t>{};
  }

  const auto size_to_read = static_cast<std::streamsize>(len);
  std::vector<uint8_t> bytes(len);
  file.read(reinterpret_cast<char *>(bytes.data()), size_to_read);
  const auto readCount = file.gcount();
  if (file.fail() && !file.eof()) return tl::make_unexpected("文件读取失败");
  if (readCount < size_to_read) bytes.resize(static_cast<size_t>(readCount));

  return std::move(bytes);
}

/**
 *
 * @brief 从文件流中读取指定长度的数据（从当前读指针位置开始），适用于无法获取文件大小的情况，超大文件会很危险
 * @param file 已打开的文件流（必须以 std::ios::binary 模式打开）
 * @param len 要读取的字节数，为 0 表示一直读到文件末尾，默认为 0
 * @param chunkSize 最小读取单元
 * @return 读取到的字节数据
 */
inline tl::expected<std::vector<uint8_t>, std::string> readSpecialFile(std::ifstream &file, const size_t len = 0, const size_t chunkSize = 4096) {
  if (!file.is_open() || file.bad()) return tl::make_unexpected("文件未打开或不可读");

  std::vector<uint8_t> data;
  if (len > 0) data.reserve(len);

  size_t remaining = len;
  while (file) {
    const size_t readSize = remaining > 0 ? std::min(remaining, chunkSize) : chunkSize;

    auto bufferOpt = readFile(file, readSize);
    bufferOpt.value<>();
    if (!bufferOpt) return tl::make_unexpected("");

    if (!bufferOpt) break;

    data.insert(data.end(), bufferOpt.value<>().begin(), bufferOpt.value<>().end());

    if (remaining > 0) {
      remaining -= bufferOpt.value<>().size();
      if (remaining == 0) break;
    }
  }
  return std::move(data);
}

inline bool write(const std::vector<uint8_t> &bytes, std::ofstream &os) {
  os.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return os.operator bool();
}

inline int64_t getFileModTime(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) return 0;
  auto fmodTime = std::filesystem::last_write_time(path);
  auto sysModTime = std::chrono::clock_cast<std::chrono::system_clock>(fmodTime);
  return std::chrono::floor<std::chrono::seconds>(sysModTime.time_since_epoch()).count();
}

inline bool mkdir(const std::filesystem::path &path) {
  if (std::filesystem::exists(path)) return true;
  return std::filesystem::create_directories(path);
}

inline bool delFile(const std::filesystem::path &filePath) {
  if (!std::filesystem::exists(filePath)) return true;
  if (std::filesystem::is_directory(filePath)) return false;
  std::error_code ec;
  std::filesystem::remove(filePath, ec);
  return !ec;
}

/**
 * @brief 删除目录（仅在路径存在且为目录时才执行操作）
 * @param dirPath 目录路径
 * @param recursion 是否递归删除目录及其所有内容，默认为 true；为 false 时目录必须为空才能删除
 * @return 删除成功（或目录不存在）返回 true，目录非空且不允许递归、路径不是目录或删除失败返回 false
 */
inline bool delDir(const std::filesystem::path &dirPath, const bool recursion = true) {
  if (!std::filesystem::exists(dirPath)) return true;
  if (!std::filesystem::is_directory(dirPath)) return false;

  std::error_code ec;
  if (recursion) {
    std::filesystem::remove_all(dirPath, ec);
    return !ec;
  }

  if (!std::filesystem::is_empty(dirPath)) return false;
  std::filesystem::remove(dirPath, ec);
  return !ec;
}

/**
 * @brief 递归复制目录（源目录中的所有内容复制到目标目录，目标目录不存在时自动创建，
 * 目标目录已存在时合并写入）
 * @param srcDir 源目录路径（必须已存在且为目录）
 * @param destDir 目标目录路径
 * @param overwrite 是否覆盖已存在的目标文件
 * @return 复制成功返回 true，任一条目失败返回 false
 */
inline bool copyDirectory(const std::filesystem::path &srcDir, const std::filesystem::path &destDir, const bool overwrite) {
  std::error_code ec;
  const auto options = overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
  std::filesystem::copy(srcDir, destDir, std::filesystem::copy_options::recursive | options, ec);
  return !ec;
}

/**
 * @brief 复制文件或目录（源不存在或类型无法识别时返回 false）
 * @param src 源文件/目录路径；为目录时递归复制其全部内容
 * @param dest 目标文件/目录路径；目标已存在且 overwrite 为 false 时直接返回 true（文件跳过，目录合并复制）
 * @param overwrite 是否覆盖目标文件，默认为 false
 * @return 复制成功返回 true，失败返回 false
 */
inline bool copy(const std::filesystem::path &src, const std::filesystem::path &dest, const bool overwrite = false) {
  if (std::filesystem::is_directory(src)) {
    if (!std::filesystem::exists(dest) && !mkdir(dest)) return false;
    return copyDirectory(src, dest, overwrite);
  }

  if (!overwrite && std::filesystem::exists(dest)) return true;
  std::error_code ec;
  std::filesystem::copy_file(src, dest, overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none, ec);
  return !ec;
}

/**
 * @brief 移动文件或目录
 * 优先使用重命名（同卷快速移动）；若失败（如跨卷）则回退为先复制再删除源的方式
 * @param src 源文件/目录路径
 * @param dest 目标文件/目录路径
 * @param overwrite 是否覆盖已存在的目标，默认为 false；为 false 且目标已存在时返回 false
 * @return 成功返回 true，失败返回 false
 */
inline bool move(const std::filesystem::path &src, const std::filesystem::path &dest, const bool overwrite = false) {
  if (!std::filesystem::exists(src)) return false;

  if (std::filesystem::exists(dest)) {
    if (!overwrite) return true;
    if (std::filesystem::is_directory(dest)) {
      if (!delDir(dest, true)) return false;
    } else if (!delFile(dest)) {
      return false;
    }
  }

  // 确保目标的父目录存在
  if (const auto parent = dest.parent_path(); !parent.empty() && !mkdir(parent)) return false;

  std::error_code ec;
  std::filesystem::rename(src, dest, ec);
  if (!ec) return true;

  // 重命名失败（可能跨卷），回退为复制 + 删除
  if (!copy(src, dest, overwrite)) return false;

  if (std::filesystem::is_directory(src)) {
    if (!delDir(src, true)) return false;
  } else if (!delFile(src)) {
    return false;
  }
  return true;
}

} // namespace alkaid::FileUtil
#endif // ALKAID_FILES_H
