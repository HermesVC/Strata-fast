@echo off
rem CUDA engine for an RTX 5060 Ti (sm_120, Blackwell) from this tree: the rc2 + Gyro line with the RAM ceiling.
rem Own folders only. build-cap, dist-cap, build-hip-win and dist are not configured and not overwritten.
rem   build-cuda.bat
rem Needs the CUDA 13.4 toolkit, Visual Studio 2022 C++ tools, and E:\agention-llama.cpp (the trellis GGML).
rem CUDA 13.2.0 / 13.2.1 miscompile sm_120; this script pins nvcc 13.4. RTX 50 needs CUDA 13, not 12.8.
title Strata CUDA build sm_120
setlocal EnableExtensions
set "SRC=E:\Strata-gyro"
set "BUILD_DIR=%SRC%\build-cuda"
set "DIST_DIR=%SRC%\dist-cuda"
set "NVCC=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\nvcc.exe"
set "CUDA_BIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\x64"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Users\Kirill\miniconda3\Scripts\cmake.exe"
set "NINJA=C:\Users\Kirill\miniconda3\Scripts\ninja.exe"
cd /d "%SRC%"
if not exist "%NVCC%" (echo missing nvcc: "%NVCC%" & exit /b 1)
if not exist "%VCVARS%" (echo missing vcvars: "%VCVARS%" & exit /b 1)
if not exist "%CMAKE%" (echo missing cmake: "%CMAKE%" & exit /b 1)
if not exist "E:\agention-llama.cpp\ggml" (echo missing GGML checkout E:\agention-llama.cpp & exit /b 1)
if not exist "%BUILD_DIR%\tmp" mkdir "%BUILD_DIR%\tmp"
set "TMP=%BUILD_DIR%\tmp"
set "TEMP=%BUILD_DIR%\tmp"
rem A HIP compiler earlier on PATH must not configure this tree.
set "HIP_PATH="
set "HIP_PLATFORM="
set "ROCM_PATH="
set "HIP_DEVICE_LIB_PATH="
set "LLVM_PATH="
set "PATH=C:\Users\Kirill\miniconda3\Scripts;C:\Users\Kirill\miniconda3;C:\Windows\System32;C:\Windows;C:\Program Files\Git\cmd"
call "%VCVARS%" >nul || exit /b 1
echo ==== %DATE% %TIME% CUDA 13.4 sm_120 ====
"%NVCC%" --version
"%CMAKE%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" -S "%SRC%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release ^
  -DSTRATA_ENABLE_CUDA=ON -DSTRATA_ENABLE_HIP=OFF -DSTRATA_BUILD_TESTS=OFF ^
  -DSTRATA_PORTABLE=ON -DSTRATA_NATIVE_EXPERTS=ON ^
  -DCMAKE_CUDA_ARCHITECTURES=120 -DCMAKE_CUDA_RUNTIME_LIBRARY=Shared ^
  "-DCMAKE_CUDA_COMPILER=%NVCC%" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DSTRATA_GGML_DIR=E:/agention-llama.cpp || exit /b 1
"%CMAKE%" --build "%BUILD_DIR%" --target strata -j 4
if errorlevel 1 (
  echo   the build stopped - trying once more
  "%CMAKE%" --build "%BUILD_DIR%" --target strata -j 4 || exit /b 1
)
set "OUT=%DIST_DIR%\strata-windows-x64-cuda"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%BUILD_DIR%\strata.exe" "%OUT%\strata.exe" >nul || exit /b 1
if exist "%BUILD_DIR%\strata.pdb" copy /y "%BUILD_DIR%\strata.pdb" "%OUT%\strata.pdb" >nul
rem cudart is shared; cuBLAS loads cublasLt, nvJitLink and nvfatbin from the same folder.
for %%D in (cudart64_13.dll cublas64_13.dll cublasLt64_13.dll nvJitLink_130_0.dll nvfatbin_130_0.dll) do (
  copy /y "%CUDA_BIN%\%%D" "%OUT%\%%D" >nul || exit /b 1
)
dumpbin /dependents "%OUT%\strata.exe"
>"%DIST_DIR%\COMMIT.txt" echo branch cuda-5060ti, CUDA 13.4, sm_120, portable AVX2, shared cudart
git -C "%SRC%" rev-parse HEAD >> "%DIST_DIR%\COMMIT.txt"
echo Done: %DIST_DIR%\strata-windows-x64-cuda\strata.exe
exit /b 0
