@echo off
rem Measure Strata's own CPU kernels for the trellis types (TQ2_T/TQK6/TQK7) - CPU only, no GPU work.
setlocal EnableDelayedExpansion
set "SRC=E:\Strata-gyro"
set "ROCM_VENV=E:\Strata-gyro\.rocm-win"
set "BUILD_DIR=E:\Strata-gyro\build-hip-win"
set "LOG=E:\Strata-gyro\tq-cpu-bench.log"
set "PATH=C:\Users\Kirill\miniconda3\Scripts;C:\Users\Kirill\miniconda3;C:\Windows\System32;C:\Windows;%PATH%"
for /f "delims=" %%R in ('"%ROCM_VENV%\Scripts\rocm-sdk.exe" path --root') do set "ROCM=%%R"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "delims=" %%V in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS=%%V"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "HIP_PLATFORM=amd"
set "PATH=%ROCM%\bin;%ROCM%\lib\llvm\bin;%PATH%"
echo ==== %DATE% %TIME% ==== >> "%LOG%"
cmake --build "%BUILD_DIR%" --target tq_cpu_parity >> "%LOG%" 2>&1
echo BUILD_EXITCODE=%ERRORLEVEL% >> "%LOG%"
"%BUILD_DIR%\tq_cpu_parity.exe" --bench >> "%LOG%" 2>&1
echo BENCH_EXITCODE=%ERRORLEVEL% >> "%LOG%"
endlocal
