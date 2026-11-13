@echo off
REM Configure + build with clang-cl + Ninja: Visual Studio's, inside its x64 environment, or without Visual
REM Studio the portable tools from get-toolchain.ps1 (in the gt5-recomp folder).
REM Usage: scripts\build.cmd [target ...]   (GT5_JOBS sets the parallel jobs; GT5_TOOLCHAIN=<folder> uses
REM get-toolchain.ps1's tools in that folder even when Visual Studio is there)
REM Not %CL%: both cl and clang-cl read that variable as extra command-line options.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "TOOLARGS="
set "TC=%GT5_TOOLCHAIN%"
if defined GT5_TOOLCHAIN goto portable
REM Visual Studio: 2022 Community where it usually is, else any edition vswhere finds that has the
REM Clang tools (Professional, Enterprise, Build Tools, newer versions).
set "VS=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if exist "%VS%\VC\Tools\Llvm\x64\bin\clang-cl.exe" goto have_vs
set "VS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath`) do set "VS=%%i"
if not defined VS goto no_vs
if not exist "%VS%\VC\Tools\Llvm\x64\bin\clang-cl.exe" goto no_vs
:have_vs
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CLANGCL=%VS:\=/%/VC/Tools/Llvm/x64/bin/clang-cl.exe"
:configure
REM A build folder set up with a compiler that's gone (Visual Studio uninstalled for the portable tools,
REM say) is set up again from scratch.
set "OLDCC="
if exist "%~dp0..\build\CMakeCache.txt" for /f "tokens=1,* delims==" %%a in ('findstr /b /c:"CMAKE_CXX_COMPILER:" "%~dp0..\build\CMakeCache.txt"') do set "OLDCC=%%b"
set "FRESH="
if defined OLDCC if not exist "%OLDCC%" set "FRESH=--fresh"
if defined FRESH del "%~dp0..\build\build.ninja" 2>nul
if not exist "%~dp0..\build\build.ninja" (
  cmake %FRESH% -S "%~dp0.." -B "%~dp0..\build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_COMPILER=%CLANGCL%" ^
    "-DCMAKE_CXX_COMPILER=%CLANGCL%" %TOOLARGS% || exit /b 1
)
REM Parallel jobs: one per thread, but at most one per 2 GB of RAM (the largest generated files need
REM about 1 GB each to compile, and running out of memory fails the build).
if not "%GT5_JOBS%"=="" goto have_jobs
set GT5_JOBS=%NUMBER_OF_PROCESSORS%
for /f %%m in ('powershell -NoProfile -Command "[math]::Max(1, [int][math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 2GB))"') do set RAMJOBS=%%m
if defined RAMJOBS if %RAMJOBS% LSS %GT5_JOBS% set GT5_JOBS=%RAMJOBS%
:have_jobs
REM The build itself, with recovery from a crashed or damaged compile: scripts\build_run.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_run.ps1" -Jobs %GT5_JOBS% %*
exit /b %ERRORLEVEL%

:no_vs
REM No Visual Studio: get-toolchain.ps1's tools, in the folder it recorded, else "toolchain" next to the
REM gt5-recomp folder.
set "TC="
if exist "%~dp0..\..\.gt5\toolchain-path.txt" set /p TC=<"%~dp0..\..\.gt5\toolchain-path.txt"
if defined TC if exist "%TC%\gt5-toolchain.txt" goto portable
set "TC=%~dp0..\..\..\toolchain"
if exist "%TC%\gt5-toolchain.txt" goto portable
echo Visual Studio with "Desktop development with C++" and "C++ Clang tools for Windows" was not found,
echo and neither were the portable tools from get-toolchain.ps1.
echo Install them with the Visual Studio Installer (see Requirements in README.md), or, to keep C: free,
echo run get-toolchain.ps1 in the gt5-recomp folder. Then run this again.
exit /b 1

:portable
REM get-toolchain.ps1's folder: LLVM, CMake and Ninja, and portable-msvc.py's MSVC libraries and Windows SDK.
REM This sets up by hand what vcvars64.bat would: INCLUDE, LIB and PATH (and VCToolsInstallDir, from which
REM clang-cl takes the MSVC version it presents as).
for %%i in ("%TC%") do set "TC=%%~fi"
if not exist "%TC%\gt5-toolchain.txt" goto bad_toolchain
set "MSVC="
for /d %%d in ("%TC%\msvc\VC\Tools\MSVC\*") do set "MSVC=%%~fd"
set "SDKVER="
for /d %%d in ("%TC%\msvc\Windows Kits\10\Lib\*") do set "SDKVER=%%~nxd"
set "SDK=%TC%\msvc\Windows Kits\10"
if not defined MSVC goto bad_toolchain
if not defined SDKVER goto bad_toolchain
if not exist "%TC%\llvm\bin\clang-cl.exe" goto bad_toolchain
set "VCToolsInstallDir=%MSVC%\"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\winrt;%SDK%\Include\%SDKVER%\cppwinrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"
set "PATH=%TC%\llvm\bin;%TC%\cmake\bin;%TC%\ninja;%SDK%\bin\%SDKVER%\x64;%PATH%"
set "CLANGCL=%TC:\=/%/llvm/bin/clang-cl.exe"
set TOOLARGS="-DCMAKE_LINKER=%TC:\=/%/llvm/bin/lld-link.exe" "-DCMAKE_RC_COMPILER=%SDK:\=/%/bin/%SDKVER%/x64/rc.exe" "-DCMAKE_MT=%SDK:\=/%/bin/%SDKVER%/x64/mt.exe"
echo Building with the portable tools in "%TC%"...
goto configure
:bad_toolchain
echo The portable tools in "%TC%" are missing or unfinished. Run get-toolchain.ps1 in the gt5-recomp folder again.
exit /b 1
