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
echo Packaging WinUpdate directory...
set "PACKAGE_DIR=WinUpdate"
if exist "%PACKAGE_DIR%" (
    echo Removing existing %PACKAGE_DIR% directory...
    attrib -r -s -h "%PACKAGE_DIR%"\*.* /s >nul 2>&1
    rmdir /s /q "%PACKAGE_DIR%"
)
mkdir "%PACKAGE_DIR%"
REM copy runtime folders if present
robocopy i18n "%PACKAGE_DIR%\i18n" /E >nul 2>&1
robocopy img "%PACKAGE_DIR%\img" /E >nul 2>&1
robocopy locale "%PACKAGE_DIR%\locale" /E >nul 2>&1
robocopy assets "%PACKAGE_DIR%\assets" /E >nul 2>&1
REM copy the built exe
if exist "%BUILD_DIR%\WinUpdate.exe" (
    copy /Y "%BUILD_DIR%\WinUpdate.exe" "%PACKAGE_DIR%\WinUpdate.exe" >nul 2>&1
) else (
    echo [WARN] Built exe not found: %BUILD_DIR%\WinUpdate.exe
)
if exist "wup_settings.txt" copy /Y "wup_settings.txt" "%PACKAGE_DIR%\wup_settings.txt" >nul 2>&1
if exist "README.md" copy /Y "README.md" "%PACKAGE_DIR%\README.md" >nul 2>&1
if exist "LICENSE.md" copy /Y "LICENSE.md" "%PACKAGE_DIR%\LICENSE.md" >nul 2>&1
echo Packaged to %PACKAGE_DIR%.
echo
echo --- Current per-user settings (%%APPDATA%%\WinUpdate\wup_settings.ini) ---
if exist "%APPDATA%\WinUpdate\wup_settings.ini" (
    type "%APPDATA%\WinUpdate\wup_settings.ini"
) else (
    echo [no file found]
)
echo -----------------------------------------------------------------
endlocal