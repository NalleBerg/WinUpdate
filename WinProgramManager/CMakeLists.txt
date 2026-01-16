cmake_minimum_required(VERSION 3.15)
project(WinProgramManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output to build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Main executable
add_executable(WinProgramManager WIN32
    main.cpp
    winprogrammanager.rc
)

# Link Windows libraries
target_link_libraries(WinProgramManager
    comctl32
    user32
    gdi32
    shell32
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
