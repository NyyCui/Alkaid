cmake_minimum_required(VERSION 3.21)

project(alkaid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ALKAID_BUILD_LEGACY_HOST "构建旧版 CEF 宿主（需要完整第三方依赖）" ON)
option(ALKAID_BUILD_HOST "构建重构版 CEF 宿主" OFF)
option(ALKAID_BUILD_PACKAGE_TOOLS "构建无 CEF 的包校验与注册表工具" OFF)
include(CTest)
add_subdirectory(core)
if(ALKAID_BUILD_HOST OR ALKAID_BUILD_PACKAGE_TOOLS)
  add_subdirectory(infra/package)
  add_subdirectory(infra/data)
  add_subdirectory(infra/network)
endif()

if(ALKAID_BUILD_LEGACY_HOST)
  add_subdirectory(libs)
  add_subdirectory(apps)
endif()

if(ALKAID_BUILD_HOST)
  if(ALKAID_BUILD_LEGACY_HOST)
    message(FATAL_ERROR "新版与旧版宿主应使用独立构建目录")
  endif()
  add_subdirectory(apps/host)
endif()
