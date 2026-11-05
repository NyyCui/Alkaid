#include "file_sink.h"

namespace alkaid::Log {

FileSink::FileSink(const std::string &filename, const uintmax_t maxSize, const int32_t maxDays)
    : logFileManager(filename, maxSize, maxDays) {
}

void FileSink::sink_it_(const spdlog::details::log_msg &msg) {
  spdlog::memory_buf_t formatted;
  formatter_->format(msg, formatted);
  logFileManager.write(formatted);
}

void FileSink::flush_() {
  logFileManager.flush();
}

} // namespace alkaid::Log
