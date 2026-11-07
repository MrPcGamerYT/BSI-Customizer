@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-SettingsPanel-Manual.ps1" %*
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo Settings-panel check failed with exit code %result%.
pause
exit /b %result%
