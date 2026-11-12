@echo off
REM Double-click to update GT5 for PC to the newest version (the same as the launcher's Update button).
REM Saves, settings, game files and the shader cache are kept. Close the game first.
cd /d "%~dp0"
title Updating Gran Turismo 5 for PC
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1" %*
echo.
pause
