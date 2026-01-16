@echo off
setlocal

REM Updated CMake build helper for `WinProgramManager`
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

REM Ensure any running WinProgramManager instance is terminated so linker can relink the EXE.
tasklist /FI "IMAGENAME eq WinProgramManager.exe" 2>NUL | find /I "WinProgramManager.exe" >NUL && (
    echo Terminating running WinProgramManager.exe...
    taskkill /IM WinProgramManager.exe /F >NUL 2>&1
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
echo Packaging WinProgramManager directory...
set "PACKAGE_DIR=WinProgramManager"
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
if exist "%BUILD_DIR%\WinProgramManager.exe" (
    copy /Y "%BUILD_DIR%\WinProgramManager.exe" "%PACKAGE_DIR%\WinProgramManager.exe" >nul 2>&1
) else (
    echo [WARN] Built exe not found: %BUILD_DIR%\WinProgramManager.exe
)
REM Copy additional files if needed
if exist "README.md" copy /Y "README.md" "%PACKAGE_DIR%\README.md" >nul 2>&1
if exist "LICENSE.md" copy /Y "LICENSE.md" "%PACKAGE_DIR%\LICENSE.md" >nul 2>&1
if exist "GPLv2.md" copy /Y "GPLv2.md" "%PACKAGE_DIR%\GPLv2.md" >nul 2>&1
if exist "GnuLogo.bmp" copy /Y "GnuLogo.bmp" "%PACKAGE_DIR%\GnuLogo.bmp" >nul 2>&1
REM Copy MinGW runtime DLLs required for standalone execution
if exist "C:\mingw64\bin\libgcc_s_seh-1.dll" copy /Y "C:\mingw64\bin\libgcc_s_seh-1.dll" "%PACKAGE_DIR%\libgcc_s_seh-1.dll" >nul 2>&1
if exist "C:\mingw64\bin\libstdc++-6.dll" copy /Y "C:\mingw64\bin\libstdc++-6.dll" "%PACKAGE_DIR%\libstdc++-6.dll" >nul 2>&1
if exist "C:\mingw64\bin\libmcfgthread-2.dll" copy /Y "C:\mingw64\bin\libmcfgthread-2.dll" "%PACKAGE_DIR%\libmcfgthread-2.dll" >nul 2>&1
REM Copy sqlite3 DLL
if exist "sqlite3\sqlite3.dll" copy /Y "sqlite3\sqlite3.dll" "%PACKAGE_DIR%\sqlite3.dll" >nul 2>&1
echo Packaged to %PACKAGE_DIR%.
echo Build complete!
echo.
echo Starting WinProgramManager...
start "" "%PACKAGE_DIR%\WinProgramManager.exe"
endlocal