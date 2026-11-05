#ifndef DEVICE_CTRL_LOGGING_H
#define DEVICE_CTRL_LOGGING_H
#include <spdlog/spdlog.h>

namespace alkaid::Log {
extern std::shared_ptr<spdlog::logger> logger;

void registerLoggingModule();
} // namespace alkaid::Log

#define LOGGER alkaid::Log::logger

#define LOGGER_DEBUG(...) \
  SPDLOG_LOGGER_DEBUG(LOGGER, __VA_ARGS__)

#define LOGGER_INFO(...) \
  SPDLOG_LOGGER_INFO(LOGGER, __VA_ARGS__)

#define LOGGER_WARN(...) \
  SPDLOG_LOGGER_WARN(LOGGER, __VA_ARGS__)

#define LOGGER_ERROR(...) \
  SPDLOG_LOGGER_ERROR(LOGGER, __VA_ARGS__)

#endif // DEVICE_CTRL_LOGGING_H
