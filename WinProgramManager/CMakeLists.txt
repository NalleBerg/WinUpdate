cmake_minimum_required(VERSION 3.15)
project(WinProgramManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output to build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Main executable
add_executable(WinProgramManager WIN32
    main.cpp
    search.cpp
    installed_apps.cpp
    winprogrammanager.rc
)

# Include SQLite3 headers
target_include_directories(WinProgramManager PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3
)

# Link Windows libraries and SQLite3
target_link_libraries(WinProgramManager
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.dll
    comctl32
    user32
    gdi32
    shell32
    shlwapi
    ole32
)

# Link options for MinGW to properly handle wWinMain
if(MINGW)
    target_link_options(WinProgramManager PRIVATE -municode)
endif()

# Enable Unicode
target_compile_definitions(WinProgramManager PRIVATE UNICODE _UNICODE)

# Compiler warnings
if(MSVC)
    target_compile_options(WinProgramManager PRIVATE /W4)
else()
    target_compile_options(WinProgramManager PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Updater executable (console version for testing)
add_executable(WinProgramUpdater
    updater_main.cpp
    WinProgramUpdater.cpp
    WinProgramUpdater.h
)

# Include SQLite3 headers
target_include_directories(WinProgramUpdater PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3
)

# Link SQLite3 DLL and Windows libraries
target_link_libraries(WinProgramUpdater
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.dll
    shell32
    ole32
)

# Link options for MinGW
if(MINGW)
    target_link_options(WinProgramUpdater PRIVATE -municode -mconsole)
endif()

# Enable Unicode
target_compile_definitions(WinProgramUpdater PRIVATE UNICODE _UNICODE _CONSOLE)

# Compiler warnings
if(MSVC)
    target_compile_options(WinProgramUpdater PRIVATE /W4)
else()
    target_compile_options(WinProgramUpdater PRIVATE -Wall -Wextra -Wpedantic)
endif()
