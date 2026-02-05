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
    app_details.cpp
    spinner_dialog.cpp
    install_dialog.cpp
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
    oleaut32
    winhttp
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

# NEW: WinProgramUpdaterGUI - Single updater with GUI (normal) and silent (--hidden) modes
# Replaces both WinProgramUpdater.exe and WinProgramUpdaterConsole.exe
add_executable(WinProgramUpdaterGUI WIN32
    updater_gui.cpp
    bouncing_ball.cpp
    bouncing_ball.h
    keyboard_shortcuts.cpp
    keyboard_shortcuts.h
    WinProgramUpdater.cpp
    WinProgramUpdater.h
    winprogrammanager.rc
)

# Include SQLite3 headers
target_include_directories(WinProgramUpdaterGUI PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3
)

# Link SQLite3 DLL and Windows libraries
target_link_libraries(WinProgramUpdaterGUI
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.dll
    comctl32
    shell32
    ole32
)

# Link options for MinGW
if(MINGW)
    target_link_options(WinProgramUpdaterGUI PRIVATE -municode -mwindows)
endif()

# Enable Unicode
target_compile_definitions(WinProgramUpdaterGUI PRIVATE UNICODE _UNICODE)

# Compiler warnings
if(MSVC)
    target_compile_options(WinProgramUpdaterGUI PRIVATE /W4)
else()
    target_compile_options(WinProgramUpdaterGUI PRIVATE -Wall -Wextra -Wpedantic)
endif()

# OLD UPDATERS (kept for now, will be removed after testing)
# Updater executable (silent/GUI version for Task Scheduler)
# NOTE: Both updater executables share the same source files (updater_main.cpp, WinProgramUpdater.cpp)
# If you modify the source, rebuild both executables!
add_executable(WinProgramUpdater WIN32
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
    target_link_options(WinProgramUpdater PRIVATE -municode -mwindows)
endif()

# Enable Unicode
target_compile_definitions(WinProgramUpdater PRIVATE UNICODE _UNICODE)

# Compiler warnings
if(MSVC)
    target_compile_options(WinProgramUpdater PRIVATE /W4)
else()
    target_compile_options(WinProgramUpdater PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Updater executable (console version for manual testing/debugging)
# NOTE: Both updater executables share the same source files (updater_main.cpp, WinProgramUpdater.cpp)
# If you modify the source, rebuild both executables!
add_executable(WinProgramUpdaterConsole
    updater_main.cpp
    WinProgramUpdater.cpp
    WinProgramUpdater.h
)

# Include SQLite3 headers
target_include_directories(WinProgramUpdaterConsole PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3
)

# Link SQLite3 DLL and Windows libraries
target_link_libraries(WinProgramUpdaterConsole
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.dll
    shell32
    ole32
)

# Link options for MinGW
if(MINGW)
    target_link_options(WinProgramUpdaterConsole PRIVATE -municode -mconsole)
endif()

# Enable Unicode
target_compile_definitions(WinProgramUpdaterConsole PRIVATE UNICODE _UNICODE _CONSOLE)

# Compiler warnings
if(MSVC)
    target_compile_options(WinProgramUpdaterConsole PRIVATE /W4)
else()
    target_compile_options(WinProgramUpdaterConsole PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Winget Helper executable (elevated process for install/reinstall/uninstall)
add_executable(winget_helper WIN32
    winget_helper.cpp
)

# Link options for MinGW
if(MINGW)
    target_link_options(winget_helper PRIVATE -municode -mwindows)
endif()

# Enable Unicode
target_compile_definitions(winget_helper PRIVATE UNICODE _UNICODE)
