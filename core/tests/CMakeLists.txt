include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.18.0
    GIT_SHALLOW    TRUE
    SYSTEM)

# MSVC: link GoogleTest against the same dynamic CRT (/MD) as our code
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(gpia_core_tests
    version_test.cpp
    frame_record_test.cpp)
target_link_libraries(gpia_core_tests PRIVATE gpia_core gpia_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(gpia_core_tests)
