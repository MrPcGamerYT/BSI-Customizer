@echo off
setlocal
if not exist "%~dp0VOID-Optimizer.exe" exit /b 1
start "" /wait "%~dp0VOID-Optimizer.exe" --unregister
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
