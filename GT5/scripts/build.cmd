@echo off
REM Configure + build with VS's clang-cl + Ninja inside a VS x64 environment.
REM Usage: scripts\build.cmd [target ...]   (GT5_JOBS sets the parallel jobs)
REM Not %CL%: both cl and clang-cl read that variable as extra command-line options.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
REM Visual Studio: 2022 Community where it usually is, else any edition vswhere finds that has the
REM Clang tools (Professional, Enterprise, Build Tools, newer versions).
set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
if exist "%VS%\VC\Tools\Llvm\x64\bin\clang-cl.exe" goto have_vs
set "VS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath`) do set "VS=%%i"
if not defined VS goto no_vs
if not exist "%VS%\VC\Tools\Llvm\x64\bin\clang-cl.exe" goto no_vs
:have_vs
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CLANGCL=%VS:\=/%/VC/Tools/Llvm/x64/bin/clang-cl.exe"
if not exist "%~dp0..\build\build.ninja" (
  cmake -S "%~dp0.." -B "%~dp0..\build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_COMPILER=%CLANGCL%" ^
    "-DCMAKE_CXX_COMPILER=%CLANGCL%" || exit /b 1
)
REM Parallel jobs: one per thread, but at most one per 2 GB of RAM (the largest generated files need
REM about 1 GB each to compile, and running out of memory fails the build).
if not "%GT5_JOBS%"=="" goto have_jobs
set GT5_JOBS=%NUMBER_OF_PROCESSORS%
for /f %%m in ('powershell -NoProfile -Command "[math]::Max(1, [int][math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 2GB))"') do set RAMJOBS=%%m
if defined RAMJOBS if %RAMJOBS% LSS %GT5_JOBS% set GT5_JOBS=%RAMJOBS%
:have_jobs
echo Building with %GT5_JOBS% parallel jobs (set GT5_JOBS to change).
call :build %* && exit /b 0
REM A compiler that runs short of memory can crash on the biggest files (the SPU jobs). Carry on
REM with half the jobs: the build picks up where it stopped.
set /a GT5_JOBS=GT5_JOBS/2
if %GT5_JOBS% LSS 1 set GT5_JOBS=1
echo The build stopped. Trying again with %GT5_JOBS% parallel jobs; it carries on where it stopped...
call :build %* && exit /b 0
exit /b 1
:build
if "%~1"=="" (
  cmake --build "%~dp0..\build" -j %GT5_JOBS% || exit /b 1
) else (
  cmake --build "%~dp0..\build" -j %GT5_JOBS% --target %* || exit /b 1
)
exit /b 0
:no_vs
echo Visual Studio with "Desktop development with C++" and "C++ Clang tools for Windows" was not found.
echo Install them with the Visual Studio Installer (see Requirements in README.md), then run this again.
exit /b 1
