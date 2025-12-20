@echo off
setlocal

REM Updated CMake build helper for `WinUpdate`
REM Usage: makeit.bat [generator] [config]
REM Examples:
REM   makeit.bat           - use default generator and Release
REM   makeit.bat "NMake Makefiles" Debug

set "GENERATOR=%~1"
set "CONFIG=%~2"

if "%GENERATOR%"=="" set "GENERATOR=MinGW Makefiles"
if "%CONFIG%"=="" set "CONFIG=Release"

echo Using generator: %GENERATOR%
echo Build configuration: %CONFIG%

REM Ensure any running WinUpdate instance is terminated so linker can relink the EXE.
tasklist /FI "IMAGENAME eq WinUpdate.exe" 2>NUL | find /I "WinUpdate.exe" >NUL && (
    echo Terminating running WinUpdate.exe...
    taskkill /IM WinUpdate.exe /F >NUL 2>&1
)

set BUILD_DIR=build
if exist %BUILD_DIR% (
    echo Removing existing build directory...
    attrib -r -s -h %BUILD_DIR%\*.* /s >nul 2>&1
    rmdir /s /q %BUILD_DIR%
)

mkdir %BUILD_DIR%

echo Configuring project with CMake...
if "%GENERATOR%"=="" (
    cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=%CONFIG%
) else (
    cmake -S . -B %BUILD_DIR% -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG%
)
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo Building (%CONFIG%)...
cmake --build %BUILD_DIR% --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo Build completed.
endlocal