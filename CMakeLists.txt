cmake_minimum_required(VERSION 3.20)
project(kairos VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Shared library ───────────────────────────────────────────────────────────
add_library(kairos_lib STATIC
    src/config.cpp
    src/rolling_window.cpp
    src/imr_window.cpp
    src/logger.cpp
    src/serial_thread.cpp
    src/stats_thread.cpp
)
target_include_directories(kairos_lib PUBLIC src)
target_link_libraries(kairos_lib PRIVATE pthread)

# ─── Pi 5 headless executable (no display) ────────────────────────────────────
add_executable(kairos src/main.cpp)
target_link_libraries(kairos PRIVATE kairos_lib pthread)

# ─── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# LVGL v9
set(LV_CONF_PATH ${CMAKE_SOURCE_DIR}/src/lv_conf.h CACHE STRING "" FORCE)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
)
FetchContent_MakeAvailable(lvgl)

# Exclude LVGL's bundled demos/examples from the default build — we don't use them.
foreach(_t lvgl_examples lvgl_demos)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()

# Remove ARM assembly files from LVGL's source list on macOS.
# LVGL's custom.cmake uses GLOB_RECURSE to pick up *.S files (Helium/NEON SIMD
# for Cortex-M55 / Cortex-A). Apple's assembler can't handle GNU ARM syntax.
if(APPLE)
    get_target_property(_lvgl_srcs lvgl SOURCES)
    list(FILTER _lvgl_srcs EXCLUDE REGEX ".*\\.S$")
    set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")
    message(STATUS "macOS: removed ARM assembly sources from LVGL")
endif()

# ─── SDL2 simulator (Mac) ─────────────────────────────────────────────────────
find_package(SDL2 QUIET)

if(SDL2_FOUND)
    message(STATUS "SDL2 found — building kairos-sim")

    # SDL2 driver sources from LVGL (all required)
    set(LVGL_SDL2_SOURCES
        ${lvgl_SOURCE_DIR}/src/drivers/sdl/lv_sdl_window.c
        ${lvgl_SOURCE_DIR}/src/drivers/sdl/lv_sdl_mouse.c
        ${lvgl_SOURCE_DIR}/src/drivers/sdl/lv_sdl_mousewheel.c
        ${lvgl_SOURCE_DIR}/src/drivers/sdl/lv_sdl_keyboard.c
    )

    add_executable(kairos-sim
        src/main.cpp
        src/display_thread.cpp
        ${LVGL_SDL2_SOURCES}
    )
    target_compile_definitions(kairos-sim PRIVATE
        KAIROS_SDL2=1
        KAIROS_DISPLAY=1
    )
    target_include_directories(kairos-sim PRIVATE
        src
        ${lvgl_SOURCE_DIR}
        ${lvgl_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(kairos-sim PRIVATE
        kairos_lib
        lvgl
        SDL2::SDL2
        pthread
    )
else()
    message(STATUS "SDL2 not found — skipping kairos-sim")
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
