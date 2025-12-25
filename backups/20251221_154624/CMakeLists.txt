cmake_minimum_required(VERSION 3.15)
project(WinUpdate VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(SOURCES main.cpp)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/logging.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/logging.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/About.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/About.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/parsing.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/parsing.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/scan_runner.cpp)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/scan_runner.cpp)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/winupdate.rc)
  list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/winupdate.rc)
endif()

add_executable(WinUpdate ${SOURCES})

# Console helper to print captured winget maps for debugging
if(EXISTS ${CMAKE_SOURCE_DIR}/tools/winget_print.cpp)
  add_executable(winget_print tools/winget_print.cpp)
  target_include_directories(winget_print PRIVATE ${CMAKE_SOURCE_DIR}/src)
  if (WIN32)
    target_link_libraries(winget_print PRIVATE comctl32)
  endif()
endif()

# Ensure headers in `src/` are found when building with root `main.cpp`
target_include_directories(WinUpdate PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(WinUpdate PROPERTIES WIN32_EXECUTABLE TRUE)

target_compile_definitions(WinUpdate PRIVATE WIN32_LEAN_AND_MEAN)

if(MSVC)
  set_target_properties(WinUpdate PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

if (WIN32)
  target_link_libraries(WinUpdate PRIVATE comctl32)
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
