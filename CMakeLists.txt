cmake_minimum_required(VERSION 3.20)
project(kairos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared library (used by both main executable and tests)
add_library(kairos_lib STATIC
    src/config.cpp
    src/rolling_window.cpp
    src/logger.cpp
    src/serial_thread.cpp
    src/stats_thread.cpp
)
target_include_directories(kairos_lib PUBLIC src)
target_link_libraries(kairos_lib PUBLIC pthread)

# Main executable
add_executable(kairos src/main.cpp)
target_link_libraries(kairos PRIVATE kairos_lib)

# Google Test via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
