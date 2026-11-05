#ifndef ALKAID_DAILY_AND_SIZE_SINK_H
#define ALKAID_DAILY_AND_SIZE_SINK_H

#include "log_file_manager.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <string>

namespace alkaid::Log {
class FileSink final : public spdlog::sinks::base_sink<std::mutex> {
  LogFileManager logFileManager;

public:
  FileSink(const std::string &filename, size_t maxSize, int32_t maxDays);

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override;

  void flush_() override;
};
} // namespace alkaid::Log

#endif // ALKAID_DAILY_AND_SIZE_SINK_H
