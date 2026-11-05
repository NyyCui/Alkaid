#include "color_format.h"

namespace alkaid::Log {
ColorFormat::ColorFormat(const bool console) : console(console) {
}

ColorFormat::ColorFormat() : console(true) {
}

void ColorFormat::format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) {
  if (!console) {
    const auto str = txt(msg);
    const auto c_str = str->c_str();

    dest.append(c_str, c_str + str->size());
    return;
  }
  switch (msg.level) {
    case spdlog::level::trace: {
      constexpr auto begin = ANSI_BLACK;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::debug: {
      constexpr auto begin = ANSI_GREEN;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::info: {
      constexpr auto begin = ANSI_BLUE;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::warn: {
      constexpr auto begin = ANSI_YELLOW;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::err: {
      constexpr auto begin = ANSI_RED;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::critical: {
      constexpr auto begin = ANSI_BRIGHT_RED;
      dest.append(begin, begin + 5);
    } break;
    case spdlog::level::off: break;
    case spdlog::level::n_levels: break;
  }

  const auto str = txt(msg);
  const auto c_str = str->c_str();

  dest.append(c_str, c_str + str->size());
  constexpr auto begin = ANSI_NONE;
  dest.append(begin, begin + 4);
}

LogMsgFormat::LogMsgFormat(const bool console) : ColorFormat(console) {
}

LogMsgFormat::LogMsgFormat() : ColorFormat(true) {
}

std::unique_ptr<spdlog::custom_flag_formatter> LogMsgFormat::clone() const {
  return spdlog::details::make_unique<LogMsgFormat>(console);
}

std::unique_ptr<std::string> LogMsgFormat::txt(const spdlog::details::log_msg &msg) {
  std::string result;
  result.reserve(msg.payload.size());
  for (const char ch : msg.payload) {
    result += ch;
  }
  return std::make_unique<std::string>(std::move(result));
}

LogLevelFormat::LogLevelFormat(const bool console) : ColorFormat(console) {
}

LogLevelFormat::LogLevelFormat() : ColorFormat(true) {
}

std::unique_ptr<spdlog::custom_flag_formatter> LogLevelFormat::clone() const {
  return spdlog::details::make_unique<LogLevelFormat>(console);
}

std::unique_ptr<std::string> LogLevelFormat::txt(const spdlog::details::log_msg &msg) {
  switch (msg.level) {
    case spdlog::level::trace:
      return std::make_unique<std::string>("TRACE");
    case spdlog::level::debug:
      return std::make_unique<std::string>("DEBUG");
    case spdlog::level::info:
      return std::make_unique<std::string>("INFO ");
    case spdlog::level::warn:
      return std::make_unique<std::string>("WARN ");
    case spdlog::level::err:
      return std::make_unique<std::string>("ERROR");
    case spdlog::level::critical:
      return std::make_unique<std::string>("CRTCL");
    default:
      return std::make_unique<std::string>("DEFLT");
  }
}
} // namespace alkaid::Log
