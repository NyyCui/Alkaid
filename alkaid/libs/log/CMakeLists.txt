set(MODULE_NAME log)

find_package(spdlog CONFIG REQUIRED)
find_package(fmt)
set(INC src)

set(LIBS
    spdlog::spdlog_header_only
    ${PROJECT_NAME}_utils
    fmt::fmt
)

set(SRCS
    src/alkaid_log/color_format.cpp
    src/alkaid_log/color_format.h
    src/alkaid_log/file_sink.cpp
    src/alkaid_log/file_sink.h
    src/alkaid_log/log_file_manager.cpp
    src/alkaid_log/log_file_manager.h
    src/alkaid_log/logging.cpp
    src/alkaid_log/logging.h
)

add_library(${PROJECT_NAME}_${MODULE_NAME} STATIC ${SRCS})

target_include_directories(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${INC})

target_link_libraries(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${LIBS})

target_compile_definitions(${PROJECT_NAME}_${MODULE_NAME} PUBLIC
                           SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
                           SPDLOG_FMT_EXTERNAL
                           LOG_PATH="logs/alkaid.log"
                           NOMINMAX # 防止 windows 库与标准库中的 min、max 冲突
                           UNICODE # 启用 windows 的宽字符 API
                           _HAS_EXCEPTIONS=0
)
