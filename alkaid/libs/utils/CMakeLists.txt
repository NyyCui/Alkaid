set(MODULE_NAME utils)

find_package(zlib-ng CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(nlohmann_json CONFIG REQUIRED)

set(INC src)

set(LIBS
    zlib-ng::zlib
    unofficial-sodium::sodium
    tl::expected
    ICU::uc ICU::i18n
    nlohmann_json::nlohmann_json
)

add_library(${PROJECT_NAME}_${MODULE_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME}_${MODULE_NAME} INTERFACE ${INC})

target_link_libraries(${PROJECT_NAME}_${MODULE_NAME} INTERFACE ${LIBS})

target_compile_definitions(${PROJECT_NAME}_${MODULE_NAME} INTERFACE
                           HOME_PATH="$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/build,${CMAKE_SOURCE_DIR}/build>"
                           NOMINMAX # 防止 windows 库与标准库中的 min、max 冲突
                           UNICODE # 启用 windows 的宽字符 API
                           _HAS_EXCEPTIONS=0
)
