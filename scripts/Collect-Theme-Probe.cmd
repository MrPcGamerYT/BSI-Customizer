@echo off
setlocal DisableDelayedExpansion
rem Read-only diagnostics: never patches HD-Player.exe or sends files anywhere.
rem Requires BSI Customizer 2.6.4+ for untruncated binary-stream previews.
rem Right-click this file and choose Run as administrator (the app requires it).
rem Optional arguments: "path to HD-Player.exe" "path to BSI-Customizer.exe".
set "ProgramRoot=%ProgramFiles%"
if defined ProgramW6432 set "ProgramRoot=%ProgramW6432%"
set "Target=%ProgramRoot%\BlueStacks_msi5\HD-Player.exe"
set "Customizer=%ProgramRoot%\BSI Customizer\BSI-Customizer.exe"
if not "%~1"=="" set "Target=%~1"
if not "%~2"=="" set "Customizer=%~2"
if not exist "%Customizer%" (
    echo BSI Customizer was not found. Install version 2.6.4 or newer first.
    echo Expected: "%Customizer%"
    pause
    exit /b 2
)
if not exist "%Target%" (
    echo HD-Player.exe was not found at: "%Target%"
    echo You can pass its full path as the first argument to this script.
    pause
    exit /b 2
)
set "ReportDir=%TEMP%\BSI-theme-probe"
if not exist "%ReportDir%" mkdir "%ReportDir%"
if not exist "%ReportDir%" (
    echo Could not create the report directory: "%ReportDir%"
    pause
    exit /b 2
)
echo Reading emulator files only. No patching or uploading takes place.
echo Reports will replace any previous reports in "%ReportDir%".
"%Customizer%" --theme-probe "%Target%" 1>"%ReportDir%\theme-probe.txt" 2>"%ReportDir%\theme-probe-errors.txt"
set "ProbeExit=%ERRORLEVEL%"
echo.
echo Probe exit code: %ProbeExit%
echo Text report: "%ReportDir%\theme-probe.txt"
echo Error report: "%ReportDir%\theme-probe-errors.txt"
echo Share only these text files, or copy and paste their contents.
echo If Windows reports elevation is required, right-click this script and
echo choose Run as administrator, then try again.
start "" notepad.exe "%ReportDir%\theme-probe.txt"
for %%F in ("%ReportDir%\theme-probe-errors.txt") do if %%~zF GTR 0 start "" notepad.exe "%%~fF"
pause
exit /b %ProbeExit%
