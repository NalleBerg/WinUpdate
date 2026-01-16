@echo off
Echo Closing Winget in systray...
.\WinUpdate\closetray.exe
Echo Starting the app in systray
start "" .\Winupdate\Winupdate.exe --systray
echo Done!