@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
if errorlevel 1 (
    echo Build failed. Please read the message above.
    pause
    exit /b 1
)
echo Build finished.
pause
