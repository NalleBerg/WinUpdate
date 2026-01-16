@echo off
REM Append C:\mingw64\bin to current User PATH using setx
setlocal enabledelayedexpansion
set "MINGW=C:\mingw64\bin"

for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "UP=%%B"

if defined UP (
    echo Current user PATH: %UP%
) else (
    set "UP="
    echo No user PATH configured for HKCU\Environment
)

echo Checking for %MINGW%
echo %UP% | findstr /I /C:"%MINGW%" >nul && (
    echo %MINGW% already in user PATH
    endlocal
    exit /b 0
)

set "NEWUP=%UP%;%MINGW%"
echo Setting user PATH to: %NEWUP%
setx PATH "%NEWUP%"
echo Done. New user PATH will apply to new processes.
endlocal