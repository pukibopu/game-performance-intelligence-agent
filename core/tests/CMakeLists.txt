include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.18.0
    GIT_SHALLOW    TRUE
    SYSTEM)

# MSVC 下让 GoogleTest 与我们的代码使用同一个动态 CRT
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(gpia_core_tests
    version_test.cpp
    frame_record_test.cpp
    result_test.cpp
    diagnostics_test.cpp
    schema_version_test.cpp
    session_manifest_test.cpp
    csv_frame_reader_test.cpp
    sha256_test.cpp
    manifest_reader_test.cpp
    run_loader_test.cpp)
target_link_libraries(gpia_core_tests PRIVATE
    gpia_core gpia_warnings GTest::gtest_main nlohmann_json::nlohmann_json)

# 测试通过绝对路径读取仓库里的固定数据
target_compile_definitions(gpia_core_tests PRIVATE
    GPIA_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
    GPIA_SCHEMAS_DIR="${PROJECT_SOURCE_DIR}/../schemas")

include(GoogleTest)
gtest_discover_tests(gpia_core_tests)
