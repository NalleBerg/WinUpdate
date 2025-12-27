echo off
echo --- Current per-user settings (%%APPDATA%%\WinUpdate\wup_settings.ini) ---
if exist "%APPDATA%\WinUpdate\wup_settings.ini" (
    type "%APPDATA%\WinUpdate\wup_settings.ini"
) else (
    echo [no file found]
)
echo -----------------------------------------------------------------