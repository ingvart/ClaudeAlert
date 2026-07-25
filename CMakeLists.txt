cmake_minimum_required(VERSION 3.24)
project(claude_usage_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Static everywhere we can (linking policy). Only the OS TLS backend stays dynamic.
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog GIT_TAG v1.14.1 GIT_SHALLOW TRUE)

# libcurl — HTTPS via the OS TLS backend (Schannel on Windows, system OpenSSL on
# Linux), so no crypto library is bundled. HTTP(S) only.
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(curl
  GIT_REPOSITORY https://github.com/curl/curl GIT_TAG curl-8_10_1 GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nlohmann_json spdlog curl)

# UI-agnostic core: credentials, OAuth refresh, HTTP, JSON, usage model.
add_library(cusage_core STATIC
  src/core/json_parse.cpp
  src/core/credentials.cpp
  src/core/oauth.cpp
  src/core/usage_client.cpp
  src/core/config.cpp
  src/core/alerts.cpp
  src/core/poller.cpp
  src/core/timefmt.cpp
  src/core/session.cpp
  src/core/session_client.cpp
  src/net/http_client.cpp
  src/net/relay.cpp
  src/platform/user_paths.cpp)
target_include_directories(cusage_core PUBLIC src)
target_link_libraries(cusage_core PUBLIC
  nlohmann_json::nlohmann_json spdlog::spdlog CURL::libcurl)
target_compile_features(cusage_core PUBLIC cxx_std_23)

if(MSVC)
  target_compile_options(cusage_core PRIVATE /W4 /permissive- /utf-8)
  # std::getenv is the portable accessor; silence MSVC's deprecation push.
  target_compile_definitions(cusage_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(cusage_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cusage src/cli/main.cpp)
target_link_libraries(cusage PRIVATE cusage_core)

# Claude Code hook forwarder: reads a hook payload on stdin, POSTs session
# metadata to the relay. Cross-platform (Windows + Linux); see PLAN.md §6.A.
add_executable(cusage_hook_notify src/hook/main.cpp)
target_link_libraries(cusage_hook_notify PRIVATE cusage_core)

# ---- Desktop tray GUI: SDL3 + Dear ImGui (optional) ----
option(CUSAGE_BUILD_GUI "Build the SDL3 + Dear ImGui tray app" ON)
if(CUSAGE_BUILD_GUI)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3 imgui)

  # Dear ImGui ships no CMake; compile its core + the SDL3/OpenGL3 backends.
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3)

  add_executable(cusage_tray src/gui/main_gui.cpp src/gui/tray_app.cpp)
  target_link_libraries(cusage_tray PRIVATE cusage_core imgui SDL3::SDL3)
  if(WIN32)
    target_link_libraries(cusage_tray PRIVATE opengl32)
    # Use our own main() with the Windows subsystem (no console window).
    target_link_options(cusage_tray PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(cusage_tray PRIVATE OpenGL::GL)
  endif()
endif()

option(CUSAGE_BUILD_TESTS "Build unit tests" ON)
if(CUSAGE_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2 GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

  enable_testing()
  add_executable(cusage_tests
    tests/test_usage_parse.cpp
    tests/test_alerts.cpp
    tests/test_config.cpp
    tests/test_timefmt.cpp
    tests/test_sessions.cpp)
  target_link_libraries(cusage_tests PRIVATE cusage_core Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(cusage_tests)
endif()
