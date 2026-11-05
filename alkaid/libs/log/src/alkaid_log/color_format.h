#ifndef DEVICE_CTRL_COLOR_FORMAT_H
#define DEVICE_CTRL_COLOR_FORMAT_H
#include <spdlog/sinks/base_sink.h>

#include "spdlog/pattern_formatter.h"

#define ANSI_NONE "\033[0m"
#define ANSI_BLACK "\033[30m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"
#define ANSI_BRIGHT_BLACK "\033[90m"
#define ANSI_BRIGHT_RED "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

namespace alkaid::Log {
class ColorFormat : public spdlog::custom_flag_formatter {
protected:
  bool console;

public:
  explicit ColorFormat(bool console);

  ColorFormat();

  void format(const spdlog::details::log_msg &msg, const std::tm &tm_time, spdlog::memory_buf_t &dest) override;

  virtual std::unique_ptr<std::string> txt(const spdlog::details::log_msg &msg) = 0;
};

class LogMsgFormat final : public ColorFormat {
public:
  explicit LogMsgFormat(bool console);

  LogMsgFormat();

  std::unique_ptr<std::string> txt(const spdlog::details::log_msg &msg) override;

  std::unique_ptr<custom_flag_formatter> clone() const override;
};

class LogLevelFormat final : public ColorFormat {
public:
  explicit LogLevelFormat(bool console);

  LogLevelFormat();

  std::unique_ptr<std::string> txt(const spdlog::details::log_msg &msg) override;

  std::unique_ptr<custom_flag_formatter> clone() const override;
};
} // namespace alkaid::Log

#endif // DEVICE_CTRL_COLOR_FORMAT_H
