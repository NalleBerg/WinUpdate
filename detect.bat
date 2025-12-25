@echo off
setlocal

set "GENERATOR=%~1"
echo start
if "%GENERATOR%"=="" (
    where g++ >nul 2>&1
    if errorlevel 1 (
        where cl.exe >nul 2>&1
        if errorlevel 1 (
            echo none
        ) else (
            echo cl
        )
    ) else (
        echo gpp
    )
)
echo done
endlocal
