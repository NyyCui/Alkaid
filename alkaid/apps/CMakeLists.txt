set(CEF_PLATFORM "windows64")
#set(CEF_VERSION "151.3.14+g5d67476+chromium-151.0.7922.72")
set(CEF_VERSION "156.0.15+gcf671c0+chromium-156.0.8078.80")
set(CEF_DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/third_party/cef")

# CEF 依赖下载与查找
execute_process(COMMAND python ${CMAKE_SOURCE_DIR}/scripts/download-cef.py ${CEF_VERSION} ${CEF_PLATFORM} ${CEF_DOWNLOAD_DIR})
set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

add_subdirectory("${CEF_ROOT}/tests/cefclient" cefclient)
add_logical_target("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
set_cef_target_out_dir()
list(APPEND CEF_CXX_COMPILER_FLAGS "/EHsc")
set(INC src)

set(LIBS
    ${PROJECT_NAME}_log
    ${PROJECT_NAME}_storage
)

set(SRCS
    src/main.cpp
    src/test.h

    src/browser/browser_app.cpp
    src/browser/browser_app.h
    src/browser/other_app.cpp
    src/browser/other_app.h
    src/browser/scheme_handler_factory.cpp
    src/browser/scheme_handler_factory.h

    src/render/sys_pubsub.cpp
    src/render/sys_pubsub.h
    src/render/sys_invoke.cpp
    src/render/sys_invoke.h
    src/render/renderer_app.cpp
    src/render/renderer_app.h
    src/render/resource_handler.cpp
    src/render/resource_handler.h

    src/client/client.cpp
    src/client/client.h
    src/client/dialog_handler.cpp
    src/client/dialog_handler.h
    src/client/js_dialog_handler.cpp
    src/client/js_dialog_handler.h
    src/client/life_span_handler.cpp
    src/client/life_span_handler.h
    src/client/request_handler.cpp
    src/client/request_handler.h

    src/window/browser_view_delegate.cpp
    src/window/browser_view_delegate.h
    src/window/window_delegate.cpp
    src/window/window_delegate.h
    src/window/window_manager.cpp
    src/window/window_manager.h

    src/app_manage/yao_pkg.cpp
    src/app_manage/yao_pkg.h
    src/app_manage/app_manage.cpp
    src/app_manage/app_manage.h

    src/sys_event/sys_event.h
    src/sys_func/sys_func.h

)

# Windows 延迟加载的 DLL 列表（减少启动时间，按需加载）
set(ADDITIONAL_DELAYLOAD_FLAGS
    /DELAYLOAD:glu32.dll
    /DELAYLOAD:oleaut32.dll
    /DELAYLOAD:opengl32.dll
)

if (USE_SANDBOX)
  # 沙箱模式：构建为 DLL，通过 bootstrap.exe 启动
  list(APPEND CEF_SHARED_LINKER_FLAGS
       ${ADDITIONAL_DELAYLOAD_FLAGS}
  )
  add_library(${PROJECT_NAME} SHARED ${SRCS})
  set_library_target_properties(${PROJECT_NAME})

  # 复制 bootstrap.exe 并重命名为目标名称，作为启动引导程序
  copy_single_file(${PROJECT_NAME} ${CEF_BINARY_DIR}/bootstrap.exe ${CEF_TARGET_OUT_DIR}/${PROJECT_NAME}.exe)

  # Visual Studio 调试器配置：启动时运行 bootstrap 生成的 exe
  if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          VS_DEBUGGER_COMMAND "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.exe"
    )
  endif ()
else ()
  # 非沙箱模式：直接构建为 Win32 可执行文件
  list(APPEND CEF_EXE_LINKER_FLAGS
       ${ADDITIONAL_DELAYLOAD_FLAGS}
  )
  add_executable(${PROJECT_NAME} WIN32 ${SRCS})
  set_executable_target_properties(${PROJECT_NAME})

  # 添加 Windows 应用程序清单（用于 UAC 和视觉样式）
  add_windows_manifest("${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_NAME}" "exe")
endif ()
# 确保 libcef_dll_wrapper 在本目标之前编译
add_dependencies(${PROJECT_NAME} libcef_dll_wrapper)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
                           STORAGE_PATH="data"
                           APP_PATH="app"
                           RESOURCES_SCHEME="${PROJECT_NAME}"
)


# 链接所有依赖库：libcef、CEF 封装库、CEF 标准库、Windows 系统库、zlib-ng
target_link_libraries(${PROJECT_NAME} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} d3d11.lib glu32.lib imm32.lib opengl32.lib ${LIBS})

# 可选：启用 ATL 时链接 oleacc（辅助功能支持）
if (USE_ATL)
  target_link_libraries(${PROJECT_NAME} oleacc.lib)
endif ()

# 复制 CEF 运行时所需的二进制文件（libcef.dll、locales 等）到输出目录
copy_files("${PROJECT_NAME}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
# 复制 CEF 资源文件（devtools_resources.pak 等）到输出目录
copy_files("${PROJECT_NAME}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# 沙箱模式下设置 LPAC（Less Privileged AppContainer）访问控制列表
if (USE_SANDBOX)
  set_lpac_acls("${PROJECT_NAME}")
endif ()

print_cef_config()
