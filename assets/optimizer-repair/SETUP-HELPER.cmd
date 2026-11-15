@echo off
setlocal
if not exist "%~dp0VOID-Optimizer.exe" goto missing
start "" /wait "%~dp0VOID-Optimizer.exe" --register
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo STOPPED: Helper registration failed. Close old helper sessions and read the error; no guard was bypassed.
if "%RC%"=="0" echo The v3 link was registered. Opening/reviewing does not apply tuning. Keep this folder in place.
pause
exit /b %RC%
:missing
echo STOPPED: Extract the complete tested ZIP first. VOID-Optimizer.exe is missing.
pause
exit /b 1
