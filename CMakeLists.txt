# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

# Statically link the MSVC runtime so the .exe runs on any Windows machine
# without installing the Visual C++ redistributable. Must be set before project().
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(MeshInspector VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MESHINSPECTOR_BUILD_APP "Build the viewer application (downloads GLFW and Dear ImGui)" ON)
option(MESHINSPECTOR_BUILD_TESTS "Build the unit tests" ON)

include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# cgltf is a single header (MIT). Used by the glTF loader.
FetchContent_Declare(cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(cgltf)

# ---------------------------------------------------------------------------
# Core library: file loaders + mesh analysis. No GUI / OpenGL dependencies.
# ---------------------------------------------------------------------------
add_library(meshinspector_core STATIC
  src/analysis.cpp
  src/objects.cpp
  src/settings.cpp
  src/mesh_io.cpp
  src/loaders/obj.cpp
  src/loaders/stl.cpp
  src/loaders/ply.cpp
  src/loaders/off.cpp
  src/loaders/threemf.cpp
  src/loaders/zip.cpp
  src/loaders/gltf.cpp)
target_include_directories(meshinspector_core PUBLIC src)
target_include_directories(meshinspector_core SYSTEM PRIVATE ${cgltf_SOURCE_DIR})
if(MSVC)
  target_compile_options(meshinspector_core PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(meshinspector_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(meshinspector_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MESHINSPECTOR_BUILD_TESTS)
  enable_testing()
  add_executable(meshinspector_tests tests/test_main.cpp)
  target_link_libraries(meshinspector_tests PRIVATE meshinspector_core)
  target_compile_definitions(meshinspector_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")
  if(MSVC)
    target_compile_options(meshinspector_tests PRIVATE /W4 /utf-8)
  endif()
  add_test(NAME meshinspector_tests COMMAND meshinspector_tests)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
if(MESHINSPECTOR_BUILD_APP)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  # Linux: X11 only (works on Wayland desktops through XWayland); avoids needing wayland-scanner.
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(glfw imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)

  set(APP_SOURCES
    src/main.cpp
    src/app.cpp
    src/ui.cpp
    src/objects_ui.cpp
    src/camera.cpp
    src/renderer.cpp
    src/gl_loader.cpp)
  if(WIN32)
    list(APPEND APP_SOURCES src/platform_win32.cpp)
  else()
    list(APPEND APP_SOURCES src/platform_other.cpp)
  endif()

  add_executable(meshinspector ${APP_SOURCES})
  target_link_libraries(meshinspector PRIVATE meshinspector_core imgui glfw)
  target_compile_definitions(meshinspector PRIVATE GLFW_INCLUDE_NONE
    MESHINSPECTOR_VERSION="${PROJECT_VERSION}")

  if(WIN32)
    target_compile_definitions(meshinspector PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
      _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
    target_link_libraries(meshinspector PRIVATE opengl32 comdlg32 shell32 user32 gdi32)
    if(MSVC)
      # GUI subsystem, but keep a plain main() entry point.
      target_link_options(meshinspector PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
      target_compile_options(meshinspector PRIVATE /W4 /utf-8 /permissive-)
      set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT meshinspector)
    else()
      target_link_options(meshinspector PRIVATE -mwindows -static)
    endif()
  else()
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(meshinspector PRIVATE Threads::Threads OpenGL::GL ${CMAKE_DL_LIBS})
    target_compile_options(meshinspector PRIVATE -Wall -Wextra)
  endif()
endif()
