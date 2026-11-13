@echo off
REM Double-click to clear GT5's shader cache (the same as the launcher's Help, Clear the shader cache).
REM The game compiles its shaders again as it needs them, so the first visit to each menu, car and
REM track stutters again for a while, as after a fresh install. Saves, settings and game files are
REM not touched. Close the game first.
cd /d "%~dp0"
title Clearing the GT5 shader cache
set "G=%~dp0GT5"
if not exist "%G%\play.bat" set "G=%~dp0."
tasklist /FI "IMAGENAME eq gt5.exe" 2>nul | find /I "gt5.exe" >nul
if not errorlevel 1 (
    echo The game is running: close it first, then run this again.
    echo.
    pause
    exit /b 1
)
set "FOUND="
for /d %%D in ("%G%\scratch\rsx_shader_cache_v*" "%G%\shadercach?") do (
    set "FOUND=1"
    for /f %%N in ('dir /a-d /s /b "%%~D" 2^>nul ^| find /c /v ""') do echo Deleting %%N files in %%~D
    rd /s /q "%%~D"
    if exist "%%~D" echo Some files in %%~D could not be deleted: close the game and try again.
)
if not defined FOUND (
    echo The shader cache is already empty.
) else (
    echo.
    echo Done. The game compiles its shaders again as you play.
)
echo.
pause
