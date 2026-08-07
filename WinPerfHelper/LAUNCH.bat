@echo off
REM Double-click me to open the launcher dialog.
REM If anything errors, the window stays open so you can read the message.

setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch.ps1"
set EC=%ERRORLEVEL%

if not "%EC%"=="0" (
    echo.
    echo [LAUNCH.bat] launch.ps1 exited with code %EC%
    echo Common causes:
    echo   - PowerShell execution blocked by group policy
    echo   - Missing .NET Framework for WinForms
    echo   - launch.ps1 syntax error / missing dependency
    echo.
    pause
)

endlocal
