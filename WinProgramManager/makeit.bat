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

REM ============================================================================
REM Backup production database FROM packaged directory - keep only 10 most recent backups
REM ============================================================================
if exist "WinProgramManager\WinProgramManager\WinProgramManager.db" (
    echo Backing up production database from packaged directory...
    if not exist "..\DB" mkdir "..\DB"
    
    REM Use PowerShell to backup and manage old backups
    powershell -NoProfile -Command "$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'; Copy-Item 'WinProgramManager\WinProgramManager\WinProgramManager.db' \"..\DB\WinProgramManager_$timestamp.db\" -Force; Write-Host \"Database backed up to: ..\DB\WinProgramManager_$timestamp.db\"; Get-ChildItem '..\DB\WinProgramManager_*.db' | Sort-Object LastWriteTime -Descending | Select-Object -Skip 10 | Remove-Item -Force -ErrorAction SilentlyContinue"
    
    echo Backup complete.
    echo.
) else (
    echo [INFO] No production database to backup.
    echo.
)
REM ============================================================================

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
    echo Preserving databases and removing old files...
    REM Backup databases if they exist
    if exist "%PACKAGE_DIR%\WinProgramManager.db" (
        copy /Y "%PACKAGE_DIR%\WinProgramManager.db" "%TEMP%\WinProgramManager_temp.db" >nul 2>&1
    )
    if exist "%PACKAGE_DIR%\WinProgramsSearch.db" (
        copy /Y "%PACKAGE_DIR%\WinProgramsSearch.db" "%TEMP%\WinProgramsSearch_temp.db" >nul 2>&1
    )
    attrib -r -s -h "%PACKAGE_DIR%"\*.* /s >nul 2>&1
    rmdir /s /q "%PACKAGE_DIR%"
    mkdir "%PACKAGE_DIR%"
    REM Restore databases if they were backed up
    if exist "%TEMP%\WinProgramManager_temp.db" (
        copy /Y "%TEMP%\WinProgramManager_temp.db" "%PACKAGE_DIR%\WinProgramManager.db" >nul 2>&1
        del "%TEMP%\WinProgramManager_temp.db" >nul 2>&1
        echo WinProgramManager.db preserved.
    )
    if exist "%TEMP%\WinProgramsSearch_temp.db" (
        copy /Y "%TEMP%\WinProgramsSearch_temp.db" "%PACKAGE_DIR%\WinProgramsSearch.db" >nul 2>&1
        del "%TEMP%\WinProgramsSearch_temp.db" >nul 2>&1
        echo WinProgramsSearch.db preserved.
    )
) else (
    mkdir "%PACKAGE_DIR%"
)
REM copy runtime folders if present
robocopy i18n "%PACKAGE_DIR%\i18n" /E >nul 2>&1
robocopy img "%PACKAGE_DIR%\img" /E >nul 2>&1
robocopy locale "%PACKAGE_DIR%\locale" /E >nul 2>&1
robocopy assets "%PACKAGE_DIR%\assets" /E >nul 2>&1
robocopy scripts "%PACKAGE_DIR%\scripts" /E >nul 2>&1
echo Scripts packaged.
REM copy the built executables
if exist "%BUILD_DIR%\WinProgramManager.exe" (
    copy /Y "%BUILD_DIR%\WinProgramManager.exe" "%PACKAGE_DIR%\WinProgramManager.exe" >nul 2>&1
) else (
    echo [WARN] Built exe not found: %BUILD_DIR%\WinProgramManager.exe
)
if exist "%BUILD_DIR%\WinProgramUpdater.exe" (
    copy /Y "%BUILD_DIR%\WinProgramUpdater.exe" "%PACKAGE_DIR%\WinProgramUpdater.exe" >nul 2>&1
    echo WinProgramUpdater.exe packaged.
) else (
    echo [WARN] WinProgramUpdater.exe not found: %BUILD_DIR%\WinProgramUpdater.exe
)
if exist "%BUILD_DIR%\WinProgramUpdaterConsole.exe" (
    copy /Y "%BUILD_DIR%\WinProgramUpdaterConsole.exe" "%PACKAGE_DIR%\WinProgramUpdaterConsole.exe" >nul 2>&1
    echo WinProgramUpdaterConsole.exe packaged.
) else (
    echo [WARN] WinProgramUpdaterConsole.exe not found: %BUILD_DIR%\WinProgramUpdaterConsole.exe
)
REM Copy additional files if needed
if exist "README.md" copy /Y "README.md" "%PACKAGE_DIR%\README.md" >nul 2>&1
if exist "LICENSE.md" copy /Y "LICENSE.md" "%PACKAGE_DIR%\LICENSE.md" >nul 2>&1
if exist "GPLv2.md" copy /Y "GPLv2.md" "%PACKAGE_DIR%\GPLv2.md" >nul 2>&1
if exist "GnuLogo.bmp" copy /Y "GnuLogo.bmp" "%PACKAGE_DIR%\GnuLogo.bmp" >nul 2>&1
REM Copy folder icon files
if exist "gr_folder.icon_closed.ico" copy /Y "gr_folder.icon_closed.ico" "%PACKAGE_DIR%\gr_folder.icon_closed.ico" >nul 2>&1
if exist "gr_folder.icon_open.ico" copy /Y "gr_folder.icon_open.ico" "%PACKAGE_DIR%\gr_folder.icon_open.ico" >nul 2>&1
REM Copy MinGW runtime DLLs required for standalone execution
if exist "C:\mingw64\bin\libgcc_s_seh-1.dll" copy /Y "C:\mingw64\bin\libgcc_s_seh-1.dll" "%PACKAGE_DIR%\libgcc_s_seh-1.dll" >nul 2>&1
if exist "C:\mingw64\bin\libstdc++-6.dll" copy /Y "C:\mingw64\bin\libstdc++-6.dll" "%PACKAGE_DIR%\libstdc++-6.dll" >nul 2>&1
if exist "C:\mingw64\bin\libwinpthread-1.dll" copy /Y "C:\mingw64\bin\libwinpthread-1.dll" "%PACKAGE_DIR%\libwinpthread-1.dll" >nul 2>&1
REM Copy sqlite3 DLL
if exist "sqlite3\sqlite3.dll" copy /Y "sqlite3\sqlite3.dll" "%PACKAGE_DIR%\sqlite3.dll" >nul 2>&1
REM Copy sqlite3 folder for scripts
if not exist "%PACKAGE_DIR%\sqlite3" mkdir "%PACKAGE_DIR%\sqlite3"
if exist "sqlite3\sqlite3.exe" copy /Y "sqlite3\sqlite3.exe" "%PACKAGE_DIR%\sqlite3\sqlite3.exe" >nul 2>&1
echo sqlite3 tools packaged.
REM Database already exists in package directory
echo Packaged to %PACKAGE_DIR%.
echo Build complete!
echo.
echo To run the updater (silent for Task Scheduler):
echo   WinProgramManager\WinProgramUpdater.exe
echo.
echo To run the updater with console output (for testing):
echo   WinProgramManager\WinProgramUpdaterConsole.exe
echo.
echo Log file:
echo   %%APPDATA%%\WinProgramManager\log\WinProgramUpdater.log
endlocal