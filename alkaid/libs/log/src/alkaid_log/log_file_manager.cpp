#include "log_file_manager.h"

#include "alkaid_utils/assert_util.h"
#include "alkaid_utils/file_util.h"
#include "alkaid_utils/local_date_time.h"

namespace alkaid::Log {
LogFileManager::LogFileManager(const std::string &filename, const uintmax_t maxSize, const int32_t maxDays)
    : historyFileRegex_(R"(^([^.]+)\.(\d{4}-\d{2}-\d{2})\.(\d+)\.([^.]+)$)"), maxSize_(maxSize), maxDays_(maxDays), currentSize_(0), currentIndex_(0) {
  namespace fs = std::filesystem;
  const fs::path logFilePath(filename);
  path_ = logFilePath.parent_path();
  name_ = logFilePath.stem().string();
  ext_ = logFilePath.extension().string();
  if (!fs::exists(path_)) fs::create_directories(path_);
  else Assert::isTrue(fs::is_directory(path_), "不是目录：{}", path_.string());

  initDateSize(logFilePath);
  initIndex();
  fileHelper.open(filename, false);
}

void LogFileManager::initDateSize(const std::filesystem::path &logFilePath) {
  namespace fs = std::filesystem;

  if (fs::exists(logFilePath)) {
    Assert::isTrue(fs::is_regular_file(logFilePath), "文件不可用：{}", logFilePath.string());
    currentDate_ = LocalDateTime::of(FileUtil::getFileModTime(logFilePath) * 1000).value().format("yyyy-MM-dd");
    currentSize_ = fs::file_size(logFilePath);
  } else {
    currentDate_ = LocalDateTime::now().format("yyyy-MM-dd");
  }
}

void LogFileManager::initIndex() {
  namespace fs = std::filesystem;
  for (const auto &entry : fs::directory_iterator(path_)) {
    if (!entry.is_regular_file()) continue;

    const std::string fname = entry.path().filename().string();
    std::smatch match;
    if (!std::regex_match(fname, match, historyFileRegex_)) continue;

    std::string name = match[1].str();
    std::string date = match[2].str();
    std::string ext = "." + match[4].str();
    if (name_ != name || ext_ != ext || date != currentDate_) continue;
    const int32_t idx = std::stoi(match[3].str()) + 1;
    if (idx > currentIndex_) currentIndex_ = idx;
  }
}

void LogFileManager::rotateFile() {
  if (currentSize_ >= maxSize_) { // 文件大小达到上限
    fileHelper.flush();
    currentSize_ = fileHelper.size();
    fileHelper.close();
    auto fname = fileHelper.filename();
    auto nname = fmt::format("{}.{}.{}{}", name_, currentDate_, currentIndex_, ext_);
    std::filesystem::rename(fname, path_ / nname);
    currentSize_ = 0;
    currentIndex_ += 1;
    fileHelper.open(fname, false);
    cleanHistory();
    return;
  }
  auto now = LocalDateTime::now().format("yyyy-MM-dd");
  if (now != currentDate_) { // 日期切换
    fileHelper.flush();
    currentSize_ = fileHelper.size();
    if (currentSize_ == 0) {
      std::filesystem::last_write_time(fileHelper.filename(), std::filesystem::file_time_type::clock::now());
      currentIndex_ = 0;
      currentDate_ = std::move(now);
    } else {
      fileHelper.close();
      auto fname = fileHelper.filename();
      auto nname = fmt::format("{}.{}.{}{}", name_, currentDate_, currentIndex_, ext_);
      std::filesystem::rename(fname, path_ / nname);
      currentSize_ = 0;
      currentIndex_ = 0;
      currentDate_ = std::move(now);
      fileHelper.open(fname, false);
      cleanHistory();
    }
  }
}
void LogFileManager::write(const spdlog::memory_buf_t &buf) {
  rotateFile();
  fileHelper.write(buf);
}
void LogFileManager::flush() {
  fileHelper.flush();
  currentSize_ = fileHelper.size();
}

void LogFileManager::cleanHistory() const {
  if (maxDays_ <= 0) return;
  const auto now = LocalDateTime::now();
  const auto cutoff = now.addDays(-maxDays_).beginOfDay();

  namespace fs = std::filesystem;
  for (const auto &entry : fs::directory_iterator(path_)) {
    if (!entry.is_regular_file()) continue;
    auto filepath = entry.path();

    std::smatch match;
    const std::string fname = filepath.filename().string();
    if (!std::regex_match(fname, match, historyFileRegex_)) continue;

    std::string name = match[1].str();
    std::string ext = "." + match[4].str();
    if (name_ != name || ext_ != ext) continue;
    std::string date = match[2].str();
    auto fileDate = LocalDateTime::parse(date, "yyyy-MM-dd");
    if (!fileDate || fileDate.value() >= cutoff) continue;
    std::filesystem::remove(filepath);
  }
}
} // namespace alkaid::Log
