# The game's launcher (Launcher.exe next to the game in a release).
#   powershell -ExecutionPolicy Bypass -File launcher\build.ps1
cmake_minimum_required(VERSION 3.25)
project(svr_launcher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 (window, 2D renderer, gamepads, file dialogs) from the ReXGlue SDK's source tree (fetched by
# tools/windows/setup.ps1), built here with the renderer and filesystem modules the SDK leaves out.
set(SVR_SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rexglue-sdk/thirdparty/sdl3")
if(NOT EXISTS "${SVR_SDL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL source not found at ${SVR_SDL_DIR}: run tools/windows/setup.ps1 first")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
add_subdirectory("${SVR_SDL_DIR}" sdl EXCLUDE_FROM_ALL)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_executable(Launcher WIN32
    src/main.cpp
    src/disc.cpp
    src/covers.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(Launcher PRIVATE "${IMGUI_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_link_libraries(Launcher PRIVATE SDL3::SDL3-static)
