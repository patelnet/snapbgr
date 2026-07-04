# SnapBGR — core pipeline + console test
#
# This CMake project builds the non-UI core (BackgroundRemovalService,
# DirectoryWatcher, Settings) as a static library plus a console test
# executable. The WinUI 3 packaged app (src/App.xaml etc.) is built from
# Visual Studio 2026 CE with the Windows App SDK 2.2.0 project templates;
# see README.md for details.
#
# Requires: CMake >= 3.24 (tested with 3.30.2), MSVC (VS 2026), vcpkg.
cmake_minimum_required(VERSION 3.24)

# Use the vcpkg toolchain automatically when VCPKG_ROOT is set and the
# caller did not already pass a toolchain file.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain file")
endif()

project(SnapBGR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(OpenCV CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# vcpkg's onnxruntime port exports this config target.
find_package(onnxruntime CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Core library (UI-independent, unit-testable)
# ---------------------------------------------------------------------------
add_library(rmbg_core STATIC
  src/BackgroundRemovalService.cpp
  src/DirectoryWatcher.cpp
  src/Settings.cpp
)
target_include_directories(rmbg_core PUBLIC src)
target_link_libraries(rmbg_core PUBLIC
  ${OpenCV_LIBS}
  nlohmann_json::nlohmann_json
  onnxruntime::onnxruntime
)

# ---------------------------------------------------------------------------
# Tray app — the real GUI app packaged by the MSI. A Win32 message-loop
# application (no WinUI dependency): system tray icon, directory watcher,
# and the background-removal pipeline. The richer WinUI 3 shell
# (src/App.xaml etc.) still builds from Visual Studio.
# ---------------------------------------------------------------------------
add_executable(rmbg_tray WIN32
  src/tray_main.cpp
  src/TrayController.cpp
  src/tray_app.rc
)
target_link_libraries(rmbg_tray PRIVATE rmbg_core)

# ---------------------------------------------------------------------------
# Console test — exercises ProcessImage with the synthetic-mask fallback so
# it works without any model binary. Used by CI.
# ---------------------------------------------------------------------------
add_executable(rmbg_console_test tests/console_test.cpp)
target_link_libraries(rmbg_console_test PRIVATE rmbg_core)

enable_testing()
add_test(NAME console_test
         COMMAND rmbg_console_test "${CMAKE_SOURCE_DIR}/assets/sample.jpg")

# ---------------------------------------------------------------------------
# Copy the model next to the binaries if one is present. The placeholder
# checked into the repo is NOT a real model; the service detects invalid
# models and falls back to the synthetic mask. Never fail the build if the
# model is missing.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/models/modnet.onnx")
  add_custom_command(TARGET rmbg_console_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/models/modnet.onnx"
            "$<TARGET_FILE_DIR:rmbg_console_test>/modnet.onnx"
    COMMENT "Copying model placeholder next to test binary")
else()
  message(STATUS "models/modnet.onnx not found - synthetic mask fallback will be used")
endif()
