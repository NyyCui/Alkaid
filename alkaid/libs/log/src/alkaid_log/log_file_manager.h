#ifndef ALKAID_LOG_FILE_MANAGER_H
#define ALKAID_LOG_FILE_MANAGER_H
#include <cstdint>
#include <filesystem>
#include <regex>
#include <spdlog/details/file_helper.h>
#include <string>

namespace alkaid::Log {

class LogFileManager {
  std::regex historyFileRegex_;            ///< 匹配历史日志文件的正则表达式
  std::filesystem::path path_;             ///< 日志存放目录
  std::string name_;                       ///< 文件名（无扩展名）
  std::string ext_;                        ///< 文件扩展名（有点）
  uintmax_t maxSize_;                      ///< 文件最大大小
  int32_t maxDays_;                        ///< 保留天数
  std::string currentDate_;                ///< 当前日期
  uintmax_t currentSize_;                  ///< 当前文件大小
  int32_t currentIndex_;                   ///< 当前文件索引
  spdlog::details::file_helper fileHelper; ///< 日志文件操作工具

  void initDateSize(const std::filesystem::path &logFilePath);
  void initIndex();
  void cleanHistory() const;
  void rotateFile();

public:
  LogFileManager(const std::string &filename, uintmax_t maxSize, int32_t maxDays);

  void write(const spdlog::memory_buf_t &buf);
  void flush();
};

} // namespace alkaid::Log

#endif // ALKAID_LOG_FILE_MANAGER_H
