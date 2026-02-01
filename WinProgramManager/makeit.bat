@echo off
setlocal

REM ============================================================================
REM MAKEIT.BAT - Complete Build and Package Script for WinProgramManager
REM ============================================================================
REM Purpose: Clean build, compile all executables, and create distribution package
REM Usage: makeit.bat [generator] [config]
REM Examples:
REM   makeit.bat                      - Use default generator (MinGW) and Release config
REM   makeit.bat "NMake Makefiles"    - Use NMake with Release config
REM   makeit.bat "MinGW Makefiles" Debug - Use MinGW with Debug config
REM
REM Package directory naming convention: Same name as parent directory (WinProgramManager)
REM This ensures consistent distribution structure across all builds
REM ============================================================================

REM ----------------------------------------------------------------------------
REM SECTION 1: Parse Command-Line Arguments and Set Defaults
REM ----------------------------------------------------------------------------
REM Parse optional generator and configuration from command line
set "GENERATOR=%~1"
set "CONFIG=%~2"

REM Define default constants - no magic strings
set "DEFAULT_GENERATOR=MinGW Makefiles"
set "DEFAULT_CONFIG=Release"

REM BEGIN: Set generator default if not provided
if "%GENERATOR%"=="" (
    set "GENERATOR=%DEFAULT_GENERATOR%"
)
REM END: Set generator default if not provided

REM BEGIN: Set configuration default if not provided
if "%CONFIG%"=="" (
    set "CONFIG=%DEFAULT_CONFIG%"
)
REM END: Set configuration default if not provided

echo ============================================================================
echo Build Configuration
echo ============================================================================
echo Generator: %GENERATOR%
echo Configuration: %CONFIG%
echo ============================================================================
echo.

REM ----------------------------------------------------------------------------
REM SECTION 2: Backup Production Database from Package Directory
REM ----------------------------------------------------------------------------
REM Purpose: Preserve user data before rebuilding package directory
REM Keeps only the 10 most recent backups to prevent disk bloat
REM Peculiarity: Uses PowerShell for timestamp generation and old backup cleanup
REM ----------------------------------------------------------------------------

REM Define backup directory constant
set "BACKUP_DIR=..\DB"
set "MAX_BACKUP_COUNT=10"

REM BEGIN: Check if production database exists in packaged directory
if exist "WinProgramManager\WinProgramManager\WinProgramManager.db" (
    echo Backing up production database from packaged directory...
    
    REM BEGIN: Ensure backup directory exists
    if not exist "%BACKUP_DIR%" (
        mkdir "%BACKUP_DIR%"
    )
    REM END: Ensure backup directory exists
    
    REM Backup with timestamp and cleanup old backups
    REM Peculiarity: PowerShell command wrapped in quotes, handles timestamp formatting and cleanup in single operation
    powershell -NoProfile -Command "$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'; Copy-Item 'WinProgramManager\WinProgramManager\WinProgramManager.db' \"%BACKUP_DIR%\WinProgramManager_$timestamp.db\" -Force; Write-Host \"Database backed up to: %BACKUP_DIR%\WinProgramManager_$timestamp.db\"; Get-ChildItem '%BACKUP_DIR%\WinProgramManager_*.db' | Sort-Object LastWriteTime -Descending | Select-Object -Skip %MAX_BACKUP_COUNT% | Remove-Item -Force -ErrorAction SilentlyContinue"
    
    echo Backup complete.
    echo.
) else (
    echo [INFO] No production database found in package directory - skipping backup.
    echo.
)
REM END: Check if production database exists in packaged directory

REM ----------------------------------------------------------------------------
REM SECTION 3: Terminate Running Application Instances
REM ----------------------------------------------------------------------------
REM Purpose: Kill any running WinProgramManager.exe and WinProgramUpdaterGUI.exe
REM         so linker can replace the executables
REM Peculiarity: Uses tasklist piping to find, then taskkill to terminate
REM Without this, build would fail with "file in use" error
REM ----------------------------------------------------------------------------

REM Define process names as constants
set "APP_PROCESS=WinProgramManager.exe"
set "UPDATER_GUI_PROCESS=WinProgramUpdaterGUI.exe"

REM BEGIN: Check if main application is running
tasklist /FI "IMAGENAME eq %APP_PROCESS%" 2>NUL | find /I "%APP_PROCESS%" >NUL && (
    echo Terminating running %APP_PROCESS% to allow rebuild...
    taskkill /IM %APP_PROCESS% /F >NUL 2>&1
    echo Process terminated.
)
REM END: Check if main application is running

REM BEGIN: Check if GUI updater is running
tasklist /FI "IMAGENAME eq %UPDATER_GUI_PROCESS%" 2>NUL | find /I "%UPDATER_GUI_PROCESS%" >NUL && (
    echo Terminating running %UPDATER_GUI_PROCESS% to allow rebuild...
    taskkill /IM %UPDATER_GUI_PROCESS% /F >NUL 2>&1
    echo GUI updater terminated.
)
REM END: Check if GUI updater is running
echo.

REM ----------------------------------------------------------------------------
REM SECTION 4: Clean and Recreate Build Directory
REM ----------------------------------------------------------------------------
REM Purpose: Ensure completely clean build (no stale object files or cache)
REM Peculiarity: Uses attrib -r -s -h to remove read-only/system/hidden flags
REM This prevents rmdir failures on protected files
REM ----------------------------------------------------------------------------

set "BUILD_DIR=build"

REM BEGIN: Remove existing build directory if it exists
if exist %BUILD_DIR% (
    echo Removing existing build directory for clean build...
    REM Remove file attributes that would prevent deletion
    attrib -r -s -h %BUILD_DIR%\*.* /s >nul 2>&1
    rmdir /s /q %BUILD_DIR%
    echo Old build directory removed.
)
REM END: Remove existing build directory if it exists

REM Create fresh build directory
mkdir %BUILD_DIR%
echo Fresh build directory created.
echo.

REM ----------------------------------------------------------------------------
REM SECTION 5: CMake Configuration
REM ----------------------------------------------------------------------------
REM Purpose: Generate build system files using specified generator
REM Peculiarity: Empty generator check allows CMake to auto-detect, but we already set default above
REM This is defensive programming in case default setting fails
REM ----------------------------------------------------------------------------

echo ============================================================================
echo Configuring CMake Project
echo ============================================================================

REM BEGIN: Configure with or without explicit generator
if "%GENERATOR%"=="" (
    REM No generator specified, let CMake auto-detect
    cmake -S . -B %BUILD_DIR% -DCMAKE_BUILD_TYPE=%CONFIG%
) else (
    REM Use specified generator
    cmake -S . -B %BUILD_DIR% -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG%
)
REM END: Configure with or without explicit generator

REM BEGIN: Check if CMake configuration succeeded
if errorlevel 1 (
    echo [ERROR] CMake configuration failed - check CMakeLists.txt and dependencies.
    exit /b 1
)
REM END: Check if CMake configuration succeeded

echo CMake configuration successful.
echo.

REM ----------------------------------------------------------------------------
REM SECTION 6: Build All Executables
REM ----------------------------------------------------------------------------
REM Purpose: Compile all targets using CMake build command
REM This builds: WinProgramManager.exe, WinProgramUpdaterGUI.exe, and legacy updaters
REM ----------------------------------------------------------------------------

echo ============================================================================
echo Building All Executables (%CONFIG% configuration)
echo ============================================================================

cmake --build %BUILD_DIR% --config %CONFIG%

REM BEGIN: Check if build succeeded
if errorlevel 1 (
    echo [ERROR] Build failed - check compiler errors above.
    exit /b 1
)
REM END: Check if build succeeded

echo Build completed successfully.
echo.

REM ============================================================================
REM SECTION 7: Create Distribution Package Directory
REM ============================================================================
REM Purpose: Assemble complete standalone distribution with all dependencies
REM Package directory convention: Same name as parent directory (WinProgramManager)
REM ============================================================================

echo ============================================================================
echo Creating Distribution Package
echo ============================================================================

REM Define package directory constant - matches parent directory name
set "PACKAGE_DIR=WinProgramManager"

REM Define temporary storage for database preservation
set "TEMP_DB_MAIN=%TEMP%\WinProgramManager_temp.db"
set "TEMP_DB_SEARCH=%TEMP%\WinProgramsSearch_temp.db"

REM ----------------------------------------------------------------------------
REM SECTION 7.1: Preserve Existing Databases During Rebuild
REM ----------------------------------------------------------------------------
REM Purpose: Keep user data when recreating package directory
REM Peculiarity: Databases are temporarily moved to %TEMP%, then restored after cleanup
REM This ensures no data loss during packaging
REM ----------------------------------------------------------------------------

REM BEGIN: Check if package directory already exists
if exist "%PACKAGE_DIR%" (
    echo Existing package directory found - preserving databases...
    
    REM BEGIN: Backup main database if it exists
    if exist "%PACKAGE_DIR%\WinProgramManager.db" (
        copy /Y "%PACKAGE_DIR%\WinProgramManager.db" "%TEMP_DB_MAIN%" >nul 2>&1
        echo - WinProgramManager.db backed up to temp location
    )
    REM END: Backup main database if it exists
    
    REM BEGIN: Backup search database if it exists
    if exist "%PACKAGE_DIR%\WinProgramsSearch.db" (
        copy /Y "%PACKAGE_DIR%\WinProgramsSearch.db" "%TEMP_DB_SEARCH%" >nul 2>&1
        echo - WinProgramsSearch.db backed up to temp location
    )
    REM END: Backup search database if it exists
    
    REM Remove file attributes that would prevent deletion
    attrib -r -s -h "%PACKAGE_DIR%"\*.* /s >nul 2>&1
    rmdir /s /q "%PACKAGE_DIR%"
    echo Old package directory removed.
    
    REM Create fresh package directory
    mkdir "%PACKAGE_DIR%"
    
    REM BEGIN: Restore main database if it was backed up
    if exist "%TEMP_DB_MAIN%" (
        copy /Y "%TEMP_DB_MAIN%" "%PACKAGE_DIR%\WinProgramManager.db" >nul 2>&1
        del "%TEMP_DB_MAIN%" >nul 2>&1
        echo - WinProgramManager.db restored from temp location
    )
    REM END: Restore main database if it was backed up
    
    REM BEGIN: Restore search database if it was backed up
    if exist "%TEMP_DB_SEARCH%" (
        copy /Y "%TEMP_DB_SEARCH%" "%PACKAGE_DIR%\WinProgramsSearch.db" >nul 2>&1
        del "%TEMP_DB_SEARCH%" >nul 2>&1
        echo - WinProgramsSearch.db restored from temp location
    )
    REM END: Restore search database if it was backed up
    
    echo Database preservation complete.
) else (
    REM Package directory doesn't exist, create it fresh
    mkdir "%PACKAGE_DIR%"
    echo Fresh package directory created.
)
REM END: Check if package directory already exists
echo.

REM ----------------------------------------------------------------------------
REM SECTION 7.2: Copy Runtime Resource Folders
REM ----------------------------------------------------------------------------
REM Purpose: Copy all runtime resource folders to package directory
REM Includes: i18n, img, locale, assets, scripts
REM Peculiarity: robocopy exit codes 0-7 are success, >7 is error (but we ignore errors with 2>&1)
REM ----------------------------------------------------------------------------

echo Copying runtime resource folders...

REM Copy internationalization resources
robocopy i18n "%PACKAGE_DIR%\i18n" /E >nul 2>&1

REM Copy image resources
robocopy img "%PACKAGE_DIR%\img" /E >nul 2>&1

REM Copy locale resources
robocopy locale "%PACKAGE_DIR%\locale" /E >nul 2>&1

REM Copy asset files
robocopy assets "%PACKAGE_DIR%\assets" /E >nul 2>&1

REM Copy PowerShell scripts
robocopy scripts "%PACKAGE_DIR%\scripts" /E >nul 2>&1

echo Resource folders packaged.
echo.

REM ----------------------------------------------------------------------------
REM SECTION 7.3: Copy Compiled Executables
REM ----------------------------------------------------------------------------
REM Purpose: Copy all built executables from build directory to package
REM Includes: Main application, new unified updater, legacy updaters
REM ----------------------------------------------------------------------------

echo Copying compiled executables...

REM BEGIN: Copy main application executable
if exist "%BUILD_DIR%\WinProgramManager.exe" (
    copy /Y "%BUILD_DIR%\WinProgramManager.exe" "%PACKAGE_DIR%\WinProgramManager.exe" >nul 2>&1
    echo - WinProgramManager.exe packaged
) else (
    echo [WARN] Main executable not found: %BUILD_DIR%\WinProgramManager.exe
)
REM END: Copy main application executable

REM BEGIN: Copy legacy updater (will be removed after testing)
if exist "%BUILD_DIR%\WinProgramUpdater.exe" (
    copy /Y "%BUILD_DIR%\WinProgramUpdater.exe" "%PACKAGE_DIR%\WinProgramUpdater.exe" >nul 2>&1
    echo - WinProgramUpdater.exe packaged (LEGACY - scheduled for removal)
) else (
    echo [INFO] Legacy WinProgramUpdater.exe not found (may have been removed)
)
REM END: Copy legacy updater

REM BEGIN: Copy legacy console updater (will be removed after testing)
if exist "%BUILD_DIR%\WinProgramUpdaterConsole.exe" (
    copy /Y "%BUILD_DIR%\WinProgramUpdaterConsole.exe" "%PACKAGE_DIR%\WinProgramUpdaterConsole.exe" >nul 2>&1
    echo - WinProgramUpdaterConsole.exe packaged (LEGACY - scheduled for removal)
) else (
    echo [INFO] Legacy WinProgramUpdaterConsole.exe not found (may have been removed)
)
REM END: Copy legacy console updater

REM BEGIN: Copy new unified GUI updater (CURRENT - replaces both legacy versions)
if exist "%BUILD_DIR%\WinProgramUpdaterGUI.exe" (
    copy /Y "%BUILD_DIR%\WinProgramUpdaterGUI.exe" "%PACKAGE_DIR%\WinProgramUpdaterGUI.exe" >nul 2>&1
    echo - WinProgramUpdaterGUI.exe packaged (NEW UNIFIED UPDATER - supports both GUI and silent modes)
) else (
    echo [WARN] Unified GUI updater not found: %BUILD_DIR%\WinProgramUpdaterGUI.exe
)
REM END: Copy new unified GUI updater

echo.

REM ----------------------------------------------------------------------------
REM SECTION 7.4: Copy Documentation and Icon Files
REM ----------------------------------------------------------------------------
REM Purpose: Package documentation, license, and icon resources
REM These files are optional - warnings only if missing
REM ----------------------------------------------------------------------------

echo Copying documentation and icon files...

REM BEGIN: Copy README documentation
if exist "README.md" (
    copy /Y "README.md" "%PACKAGE_DIR%\README.md" >nul 2>&1
    echo - README.md packaged
)
REM END: Copy README documentation

REM BEGIN: Copy LICENSE file
if exist "LICENSE.md" (
    copy /Y "LICENSE.md" "%PACKAGE_DIR%\LICENSE.md" >nul 2>&1
    echo - LICENSE.md packaged
)
REM END: Copy LICENSE file

REM BEGIN: Copy GPLv2 license
if exist "GPLv2.md" (
    copy /Y "GPLv2.md" "%PACKAGE_DIR%\GPLv2.md" >nul 2>&1
    echo - GPLv2.md packaged
)
REM END: Copy GPLv2 license

REM BEGIN: Copy GNU logo bitmap
if exist "GnuLogo.bmp" (
    copy /Y "GnuLogo.bmp" "%PACKAGE_DIR%\GnuLogo.bmp" >nul 2>&1
    echo - GnuLogo.bmp packaged
)
REM END: Copy GNU logo bitmap

REM BEGIN: Copy closed folder icon
if exist "gr_folder.icon_closed.ico" (
    copy /Y "gr_folder.icon_closed.ico" "%PACKAGE_DIR%\gr_folder.icon_closed.ico" >nul 2>&1
    echo - gr_folder.icon_closed.ico packaged
)
REM END: Copy closed folder icon

REM BEGIN: Copy open folder icon
if exist "gr_folder.icon_open.ico" (
    copy /Y "gr_folder.icon_open.ico" "%PACKAGE_DIR%\gr_folder.icon_open.ico" >nul 2>&1
    echo - gr_folder.icon_open.ico packaged
)
REM END: Copy open folder icon

echo.

REM ----------------------------------------------------------------------------
REM SECTION 7.5: Copy MinGW Runtime Dependencies
REM ----------------------------------------------------------------------------
REM Purpose: Include C++ runtime DLLs for standalone execution without MinGW installed
REM Required: libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll
REM Peculiarity: Absolute paths to C:\mingw64\bin - assumes MinGW64 installation
REM Without these, application fails with "missing DLL" error on clean Windows systems
REM ----------------------------------------------------------------------------

echo Copying MinGW runtime dependencies...

REM Define MinGW bin directory constant
set "MINGW_BIN=C:\mingw64\bin"

REM BEGIN: Copy GCC exception handling DLL
if exist "%MINGW_BIN%\libgcc_s_seh-1.dll" (
    copy /Y "%MINGW_BIN%\libgcc_s_seh-1.dll" "%PACKAGE_DIR%\libgcc_s_seh-1.dll" >nul 2>&1
    echo - libgcc_s_seh-1.dll packaged (GCC exception handling)
) else (
    echo [WARN] MinGW runtime not found: %MINGW_BIN%\libgcc_s_seh-1.dll
)
REM END: Copy GCC exception handling DLL

REM BEGIN: Copy C++ standard library DLL
if exist "%MINGW_BIN%\libstdc++-6.dll" (
    copy /Y "%MINGW_BIN%\libstdc++-6.dll" "%PACKAGE_DIR%\libstdc++-6.dll" >nul 2>&1
    echo - libstdc++-6.dll packaged (C++ standard library)
) else (
    echo [WARN] C++ standard library not found: %MINGW_BIN%\libstdc++-6.dll
)
REM END: Copy C++ standard library DLL

REM BEGIN: Copy POSIX threading DLL
if exist "%MINGW_BIN%\libwinpthread-1.dll" (
    copy /Y "%MINGW_BIN%\libwinpthread-1.dll" "%PACKAGE_DIR%\libwinpthread-1.dll" >nul 2>&1
    echo - libwinpthread-1.dll packaged (POSIX threading support)
) else (
    echo [WARN] Threading library not found: %MINGW_BIN%\libwinpthread-1.dll
)
REM END: Copy POSIX threading DLL

echo.

REM ----------------------------------------------------------------------------
REM SECTION 7.6: Copy SQLite3 Dependencies
REM ----------------------------------------------------------------------------
REM Purpose: Package SQLite3 runtime DLL and command-line tool
REM DLL: Required for database operations in executables
REM EXE: Used by PowerShell scripts for database maintenance
REM Peculiarity: sqlite3.exe goes in subdirectory, sqlite3.dll in root
REM ----------------------------------------------------------------------------

echo Copying SQLite3 dependencies...

set "SQLITE_SOURCE=sqlite3"

REM BEGIN: Copy SQLite3 DLL for runtime database operations
if exist "%SQLITE_SOURCE%\sqlite3.dll" (
    copy /Y "%SQLITE_SOURCE%\sqlite3.dll" "%PACKAGE_DIR%\sqlite3.dll" >nul 2>&1
    echo - sqlite3.dll packaged (database runtime)
) else (
    echo [WARN] SQLite3 DLL not found: %SQLITE_SOURCE%\sqlite3.dll
)
REM END: Copy SQLite3 DLL for runtime database operations

REM BEGIN: Create sqlite3 tools subdirectory
if not exist "%PACKAGE_DIR%\sqlite3" (
    mkdir "%PACKAGE_DIR%\sqlite3"
    echo - sqlite3 subdirectory created
)
REM END: Create sqlite3 tools subdirectory

REM BEGIN: Copy SQLite3 command-line tool for scripts
if exist "%SQLITE_SOURCE%\sqlite3.exe" (
    copy /Y "%SQLITE_SOURCE%\sqlite3.exe" "%PACKAGE_DIR%\sqlite3\sqlite3.exe" >nul 2>&1
    echo - sqlite3.exe packaged (command-line tool for scripts)
) else (
    echo [WARN] SQLite3 CLI not found: %SQLITE_SOURCE%\sqlite3.exe
)
REM END: Copy SQLite3 command-line tool for scripts

echo.

REM ============================================================================
REM SECTION 8: Build Complete - Display Summary
REM ============================================================================
REM Purpose: Inform user of successful packaging and available executables
REM ============================================================================

echo ============================================================================
echo BUILD AND PACKAGE COMPLETE
echo ============================================================================
echo Package directory: %PACKAGE_DIR%
echo.
echo Database Status:
echo   - Existing databases preserved (if any)
echo   - Production database backed up to: %BACKUP_DIR%
echo.
echo ============================================================================
echo AVAILABLE EXECUTABLES
echo ============================================================================
echo.
echo MAIN APPLICATION:
echo   %PACKAGE_DIR%\WinProgramManager.exe
echo.
echo NEW UNIFIED UPDATER (Current):
echo   %PACKAGE_DIR%\WinProgramUpdaterGUI.exe
echo   - GUI Mode: Double-click or run without parameters
echo   - Silent Mode: Run with --hidden flag (for Task Scheduler)
echo.
echo LEGACY UPDATERS (Scheduled for removal after testing):
echo   %PACKAGE_DIR%\WinProgramUpdater.exe
echo   %PACKAGE_DIR%\WinProgramUpdaterConsole.exe
echo.
echo ============================================================================
echo.

endlocal