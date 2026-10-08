@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Test
if errorlevel 1 (
    echo Verification failed.
    pause
    exit /b 1
)
pause
