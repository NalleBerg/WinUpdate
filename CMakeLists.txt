cmake_minimum_required(VERSION 3.15)
project(WinUpdate VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

## Always build using the root `main.cpp` to match project's build scripts.
set(SOURCES main.cpp)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/logging.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/logging.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/About.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/About.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/hyperlink.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/hyperlink.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/skip_confirm_dialog.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/skip_confirm_dialog.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/skip_update.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/skip_update.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/globals.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/globals.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/parsing.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/parsing.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/unskip.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/unskip.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/Config.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/Config.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/hidden_scan.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/hidden_scan.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/startup_manager.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/startup_manager.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/system_tray.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/system_tray.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/ctrlw.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/ctrlw.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/install_dialog.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/install_dialog.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/exclude.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/exclude.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/unexclude_dialog.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/unexclude_dialog.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/exclude_confirm_dialog.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/exclude_confirm_dialog.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/build/tooltips.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/build/tooltips.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/winupdate.rc)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/winupdate.rc)
endif()

add_executable(WinUpdate ${SOURCES})

# Ensure headers in `src/` are found when building with root `main.cpp`
target_include_directories(WinUpdate PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(EXISTS ${CMAKE_SOURCE_DIR}/build)
  target_include_directories(WinUpdate PRIVATE ${CMAKE_SOURCE_DIR}/build)
endif()

set_target_properties(WinUpdate PROPERTIES WIN32_EXECUTABLE TRUE)

target_compile_definitions(WinUpdate PRIVATE WIN32_LEAN_AND_MEAN)

if(MSVC)
  set_target_properties(WinUpdate PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

if (WIN32)
  target_link_libraries(WinUpdate PRIVATE comctl32 shlwapi ole32 oleaut32 gdiplus)
  # Add security flags to reduce false positives from Windows Defender
  target_link_options(WinUpdate PRIVATE
    -Wl,--dynamicbase    # Enable ASLR (Address Space Layout Randomization)
    -Wl,--nxcompat       # Enable DEP (Data Execution Prevention)
    -Wl,--high-entropy-va # Enable 64-bit ASLR
  )
endif()

# Build closetray.exe - small utility to close WinUpdate from system tray
if(EXISTS ${CMAKE_SOURCE_DIR}/closetray.cpp)
  add_executable(closetray closetray.cpp)
  set_target_properties(closetray PROPERTIES
    WIN32_EXECUTABLE FALSE  # Console application
  )
endif()

# Build winget_helper.exe - elevated helper for running winget commands
if(EXISTS ${CMAKE_SOURCE_DIR}/winget_helper.cpp)
  add_executable(winget_helper WIN32 winget_helper.cpp)
  set_target_properties(winget_helper PROPERTIES
    WIN32_EXECUTABLE TRUE  # GUI application (no console window)
    LINK_FLAGS "-municode"
  )
endif()

# Build test_install_overlay.exe - test app for install UI
if(EXISTS ${CMAKE_SOURCE_DIR}/test_install_overlay.cpp)
  add_executable(test_install_overlay test_install_overlay.cpp)
  set_target_properties(test_install_overlay PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "-municode"
  )
  target_link_libraries(test_install_overlay PRIVATE comctl32)
endif()

# Fetch nlohmann/json for robust JSON parsing
option(USE_FETCH_NLOHMANN "Fetch nlohmann/json via FetchContent" OFF)
if(USE_FETCH_NLOHMANN)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
  )
  FetchContent_MakeAvailable(nlohmann_json)
  target_link_libraries(WinUpdate PRIVATE nlohmann_json::nlohmann_json)
else()
  # Not fetching nlohmann by default; main.cpp will fall back to the ad-hoc parser
endif()
