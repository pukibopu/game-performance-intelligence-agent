cmake_minimum_required(VERSION 3.25)

project(gpia_core
    VERSION 0.1.0
    DESCRIPTION "GPIA performance analysis core"
    LANGUAGES CXX)

option(GPIA_BUILD_TESTS "Build unit tests" ON)
option(GPIA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

# ---------- 第三方依赖 ----------
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
FetchContent_MakeAvailable(nlohmann_json)

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
    src/diagnostics.cpp
    src/schema_version.cpp
    src/model/session_manifest.cpp
    src/io/file_io.cpp
    src/io/text.cpp
    src/io/sha256.cpp
    src/io/csv_frame_reader.cpp
    src/io/manifest_reader.cpp
    src/io/run_loader.cpp
    src/stats/summary.cpp
    src/detect/spike_detector.cpp)

target_include_directories(gpia_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gpia_core PUBLIC cxx_std_20)
target_compile_definitions(gpia_core PRIVATE GPIA_CORE_VERSION="${PROJECT_VERSION}")
target_link_libraries(gpia_core PRIVATE gpia_warnings nlohmann_json::nlohmann_json)

# ---------- 命令行程序 ----------
add_executable(gpia_cli cli/main.cpp)
set_target_properties(gpia_cli PROPERTIES OUTPUT_NAME gpia-core)
target_link_libraries(gpia_cli PRIVATE gpia_core gpia_warnings)

# ---------- 测试 ----------
if(GPIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
