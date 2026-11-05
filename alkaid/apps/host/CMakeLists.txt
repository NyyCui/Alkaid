# 新宿主仅依赖运行核心和 CEF，JSON 与 IPC 使用 CEF 的公开接口。
file(READ "${CMAKE_SOURCE_DIR}/cef.lock.json" CEF_LOCK)
string(JSON ALKAID_CEF_ARCHIVE GET "${CEF_LOCK}" archive)
string(REPLACE ".tar.bz2" "" ALKAID_CEF_DIRECTORY "${ALKAID_CEF_ARCHIVE}")
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef/${ALKAID_CEF_DIRECTORY}")
if(NOT EXISTS "${CEF_ROOT}/.alkaid-verified.json")
  message(FATAL_ERROR "请先执行 python scripts/prepare-cef.py 准备经过校验的 CEF")
endif()
set(CEF_PLATFORM windows64)
set(USE_SANDBOX ON CACHE BOOL "使用 bootstrap 沙箱" FORCE)
set(USE_ATL OFF CACHE BOOL "当前宿主不依赖 ATL" FORCE)
set(CEF_RUNTIME_LIBRARY_FLAG /MD)
# 避免编译期 PDB 服务依赖，调试信息嵌入目标文件，仍保留运行时沙箱。
set(CEF_DEBUG_INFO_FLAG /Z7 CACHE STRING "使用内嵌调试信息" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)
add_logical_target(libcef_lib "${CEF_LIB_RELEASE}" "${CEF_LIB_RELEASE}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(ALKAID_NODE_EXECUTABLE NAMES node REQUIRED)
set(ALKAID_TYPESCRIPT_JS "${CMAKE_SOURCE_DIR}/packages/sdk/node_modules/typescript/lib/tsc.js" CACHE FILEPATH "TypeScript 编译器")
if(NOT EXISTS "${ALKAID_TYPESCRIPT_JS}")
  message(FATAL_ERROR "请在 packages/sdk 执行 npm ci 安装开发依赖")
endif()
add_custom_target(alkaid_web ALL
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build-desktop.py"
    --node "${ALKAID_NODE_EXECUTABLE}" --typescript "${ALKAID_TYPESCRIPT_JS}"
    --output "${CMAKE_BINARY_DIR}/web"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_BINARY_DIR}/web" "${CMAKE_BINARY_DIR}/run/web"
  COMMENT "构建独立桌面与 SDK"
  VERBATIM)

add_library(alkaid_host SHARED main.cpp tray_icon.cpp host_app.cpp host_client.cpp host_window.cpp resource_service.cpp app_context.cpp app_runtime.cpp package_manager.cpp close_coordinator.cpp plugin_process.cpp)
set_library_target_properties(alkaid_host)
target_include_directories(alkaid_host PRIVATE "${CEF_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(alkaid_host PRIVATE /utf-8)
# CEF 的通用延迟加载列表包含本目标未使用的系统库。
target_link_options(alkaid_host PRIVATE /IGNORE:4199)
target_compile_definitions(alkaid_host PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_definitions(alkaid_host PRIVATE "ALKAID_PACK_NODE=L\"${ALKAID_NODE_EXECUTABLE}\"")
target_link_libraries(alkaid_host PRIVATE alkaid_runtime alkaid_packages alkaid_data alkaid_network libcef_lib libcef_dll_wrapper advapi32 crypt32 shell32 ${CEF_STANDARD_LIBS})
add_custom_target(alkaid_developer_tools
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/run/developer"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/pack-ypkg.ps1"
    "${CMAKE_SOURCE_DIR}/scripts/ypkg.mjs" "${CMAKE_SOURCE_DIR}/scripts/write-ypkg.py"
    "$<TARGET_FILE:alkaid_package_tool>" "${CMAKE_BINARY_DIR}/run/developer"
  DEPENDS alkaid_package_tool
  VERBATIM)
add_dependencies(alkaid_host alkaid_web alkaid_developer_tools)
set_target_properties(alkaid_host PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/run"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/run"
  VS_DEBUGGER_COMMAND "${CMAKE_BINARY_DIR}/run/alkaid_host.exe")
set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/run")
copy_files(alkaid_host "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR_RELEASE}" "${CEF_TARGET_OUT_DIR}")
copy_files(alkaid_host "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
copy_single_file(alkaid_host "${CEF_BINARY_DIR_RELEASE}/bootstrap.exe" "${CEF_TARGET_OUT_DIR}/alkaid_host.exe")
add_custom_command(TARGET alkaid_host POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/embed-icon.py"
    "${CEF_TARGET_OUT_DIR}/alkaid_host.exe" "${CMAKE_SOURCE_DIR}/../desktop/assets/alkaid.ico"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CEF_ROOT}/LICENSE.txt" "${CEF_TARGET_OUT_DIR}/CEF-LICENSE.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CEF_ROOT}/CREDITS.html" "${CEF_TARGET_OUT_DIR}/CEF-CREDITS.html"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CEF_TARGET_OUT_DIR}/licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/package/miniz/LICENSE" "${CEF_TARGET_OUT_DIR}/licenses/miniz-LICENSE.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/package/json/LICENSE.MIT" "${CEF_TARGET_OUT_DIR}/licenses/json-LICENSE.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/package/NOTICE.md" "${CEF_TARGET_OUT_DIR}/licenses/package-NOTICE.md"
  VERBATIM)
set_lpac_acls(alkaid_host)
