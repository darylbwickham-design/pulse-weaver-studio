@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
 echo Install Node.js 22 or newer before setup.
 pause
 exit /b 1
)
node setup-obs.cjs
pause
