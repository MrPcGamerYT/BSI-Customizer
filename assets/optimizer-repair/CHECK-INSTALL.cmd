@echo off
setlocal
powershell.exe -NoLogo -NoProfile -File "%~dp0INSTALL-OPTIMIZER.ps1" -Mode Check %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo STOPPED: Read the diagnostic above. No safety check was bypassed.
pause
exit /b %RC%
