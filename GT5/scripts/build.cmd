@echo off
REM Configure + build with VS's clang-cl + Ninja inside a VS x64 environment.
REM Usage: scripts\build.cmd [target ...]   (GT5_JOBS sets the parallel jobs)
REM Not %CL%: both cl and clang-cl read that variable as extra command-line options.
set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CLANGCL=%VS:\=/%/VC/Tools/Llvm/x64/bin/clang-cl.exe"
if not exist "%~dp0..\build\build.ninja" (
  cmake -S "%~dp0.." -B "%~dp0..\build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_COMPILER=%CLANGCL%" ^
    "-DCMAKE_CXX_COMPILER=%CLANGCL%" || exit /b 1
)
if "%GT5_JOBS%"=="" set GT5_JOBS=24
if "%~1"=="" (
  cmake --build "%~dp0..\build" -j %GT5_JOBS% || exit /b 1
) else (
  cmake --build "%~dp0..\build" -j %GT5_JOBS% --target %* || exit /b 1
)
