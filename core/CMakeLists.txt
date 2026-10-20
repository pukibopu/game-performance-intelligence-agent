cmake_minimum_required(VERSION 3.25)

project(gpia_core
    VERSION 0.1.0
    DESCRIPTION "GPIA performance analysis core"
    LANGUAGES CXX)

option(GPIA_BUILD_TESTS "Build unit tests" ON)
option(GPIA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

# ---------- 只作用于本项目 target 的警告设置 ----------
add_library(gpia_warnings INTERFACE)
target_compile_options(gpia_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8 /Zc:__cplusplus>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

if(GPIA_WARNINGS_AS_ERRORS)
    target_compile_options(gpia_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
endif()

# ---------- 核心库 ----------
add_library(gpia_core STATIC
    src/version.cpp
    src/diagnostics.cpp)

target_include_directories(gpia_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gpia_core PUBLIC cxx_std_20)
target_compile_definitions(gpia_core PRIVATE GPIA_CORE_VERSION="${PROJECT_VERSION}")
target_link_libraries(gpia_core PRIVATE gpia_warnings)

# ---------- 命令行程序 ----------
add_executable(gpia_cli cli/main.cpp)
set_target_properties(gpia_cli PROPERTIES OUTPUT_NAME gpia-core)
target_link_libraries(gpia_cli PRIVATE gpia_core gpia_warnings)

# ---------- 测试 ----------
if(GPIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
