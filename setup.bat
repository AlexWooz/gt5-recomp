@echo off
rem Double-click to set up the port, or to carry on where setup stopped. It does the same as
rem   powershell -ExecutionPolicy Bypass -File setup.ps1
rem and keeps the window open at the end so you can read what it says.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
echo.
pause
