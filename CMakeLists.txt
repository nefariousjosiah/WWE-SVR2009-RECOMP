# svr2009 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(svr2009 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SVR2009_SOURCES
    src/main.cpp
    src/fps_hooks.cpp
    src/game_locator.cpp
    src/input_script.cpp
)

if(WIN32)
    add_executable(svr2009 WIN32 ${SVR2009_SOURCES})
else()
    add_executable(svr2009 ${SVR2009_SOURCES})
endif()

rexglue_setup_target(svr2009 GPU_PLUGINS xenos)
if(WIN32)
    # Game file picker (src/game_locator.cpp).
    target_link_libraries(svr2009 PRIVATE comdlg32)
endif()

# Developer call census of the D3D library functions (tools/native/gen_census.py), for native
# renderer reverse engineering. Off by default; never ship a build with it on.
option(SVR_D3D_CENSUS "Count calls to the game's D3D library functions" OFF)
# Native renderer (cmake/native_renderer.cmake, docs/native-renderer.md). Its hooks override the
# same D3D functions the census wraps, so the two are exclusive.
option(SVR_NATIVE_RENDERER "Draw through plume instead of emulating the Xenos GPU" OFF)
if(SVR_NATIVE_RENDERER AND SVR_D3D_CENSUS)
    message(FATAL_ERROR "SVR_NATIVE_RENDERER and SVR_D3D_CENSUS hook the same functions; pick one")
endif()
if(SVR_D3D_CENSUS)
    target_sources(svr2009 PRIVATE src/dev/d3d_census.cpp src/dev/shader_dump.cpp)
endif()
if(SVR_NATIVE_RENDERER)
    include(cmake/native_renderer.cmake)
    svr_add_native_renderer(svr2009)
endif()

# Overlay fonts (Press Start 2P for the FPS counter, SIL OFL 1.1) next to the executable.
add_custom_command(TARGET svr2009 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts" "$<TARGET_FILE_DIR:svr2009>/fonts")
