@echo off
rem Build native_expert_parity from the same HIP build tree, then verify the Gyro-M GGUF against the pack.
setlocal EnableDelayedExpansion
set "SRC=E:\Strata-gyro"
set "ROCM_VENV=E:\Strata-gyro\.rocm-win"
set "BUILD_DIR=E:\Strata-gyro\build-cap"
set "LOG=E:\Strata-gyro\parity-gyro-m.log"
set "GGUF=E:\Strata-data\models\GYRO_M\Qwen3.8-Flash-Next-Gyro-M-TQ2_0.gguf"
set "PACK=E:\Strata-data\packs\gyro-m"
set "PIP_CACHE_DIR=E:\pip-cache"
set "PATH=C:\Users\Kirill\miniconda3\Scripts;C:\Users\Kirill\miniconda3;C:\Windows\System32;C:\Windows;%PATH%"

for /f "delims=" %%R in ('"%ROCM_VENV%\Scripts\rocm-sdk.exe" path --root') do set "ROCM=%%R"
set "ROCM_F=%ROCM:\=/%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
for /f "delims=" %%V in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS=%%V"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "HIP_PLATFORM=amd"
set "HIP_PATH=%ROCM%"
set "ROCM_PATH=%ROCM%"
set "PATH=%ROCM%\bin;%ROCM%\lib\llvm\bin;%PATH%"

echo ==== %DATE% %TIME% build ==== >> "%LOG%"
cmake --build "%BUILD_DIR%" --target native_expert_parity >> "%LOG%" 2>&1
echo BUILD_EXITCODE=%ERRORLEVEL% >> "%LOG%"
if not "%ERRORLEVEL%"=="0" goto :end

echo ==== %DATE% %TIME% load-check ==== >> "%LOG%"
set "STRATA_GGUF_PY=E:\agention-llama.cpp\gguf-py"
"%BUILD_DIR%\native_expert_parity.exe" --load-check "%PACK%" "%GGUF%" >> "%LOG%" 2>&1
echo CHECK_EXITCODE=%ERRORLEVEL% >> "%LOG%"
:end
endlocal
