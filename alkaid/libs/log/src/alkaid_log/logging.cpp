#include "logging.h"
#include "color_format.h"
#include "file_sink.h"
#include "alkaid_utils/process_util.h"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "alkaid_utils/module_registry.h"

#define CONSOLE_PATTERN ANSI_MAGENTA "%Y-%m-%d %H:%M:%S.%e" ANSI_NONE " %-5l" ANSI_BLUE " [ %-6t" ANSI_CYAN " %s:%#" ANSI_BLUE " %! ]" ANSI_NONE ": %v"
#define FILE_PATTERN "%Y-%m-%d %H:%M:%S.%e %-5l [ %-6t %s:%# %! ]: %v"

namespace alkaid::Log {
std::shared_ptr<spdlog::logger> logger = nullptr;

void registerLoggingModule() {
  ModuleRegistry::registerModule({
      "Log",
      "",
      {},
      []() -> tl::expected<void, std::string> {
        const auto file_sink = std::make_shared<FileSink>((ProcessUtil::homeDir() / LOG_PATH).string(), 50 * 1024 * 1024, 15);
        constexpr auto level = static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL);
        auto file_formatter = std::make_unique<spdlog::pattern_formatter>();
        file_formatter->add_flag<LogLevelFormat>('l', false).set_pattern(FILE_PATTERN);
        file_sink->set_formatter(std::move(file_formatter));

        auto console_formatter = std::make_unique<spdlog::pattern_formatter>();
        console_formatter->add_flag<LogLevelFormat>('l').add_flag<LogMsgFormat>('v').set_pattern(CONSOLE_PATTERN);
        const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_formatter(std::move(console_formatter));

        spdlog::sinks_init_list sinks = {console_sink, file_sink};
        logger = std::make_shared<spdlog::logger>("alkaid", sinks);
        logger->set_level(level);
        logger->flush_on(level);
        spdlog::set_default_logger(logger);
        return {};
      },
      [] {
        logger.reset();
        logger = nullptr;
      }
    }
  );
}
} // namespace alkaid::Log
