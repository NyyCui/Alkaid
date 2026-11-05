set(MODULE_NAME storage)

set(INC src)
set(LIBS
    ${PROJECT_NAME}_db
)

set(SRCS
    src/alkaid_storage/storage.h
    src/alkaid_storage/storage.cpp
    src/alkaid_storage/sys/system_config.h
    src/alkaid_storage/sys/system_config.cpp
    src/alkaid_storage/sys/organization.h
    src/alkaid_storage/sys/organization.cpp
    src/alkaid_storage/sys/program.h
    src/alkaid_storage/sys/program.cpp
)

add_library(${PROJECT_NAME}_${MODULE_NAME} STATIC ${SRCS})

target_include_directories(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${INC})

target_link_libraries(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${LIBS})

target_compile_definitions(${PROJECT_NAME}_${MODULE_NAME} PRIVATE
                           STORAGE_PATH="data"
                           NOMINMAX # 防止 windows 库与标准库中的 min、max 冲突
                           UNICODE # 启用 windows 的宽字符 API
                           _HAS_EXCEPTIONS=0
)