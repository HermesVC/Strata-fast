@echo off
rem Build the RAM-ceiling engine (f-143-memlog, STRATA_RAM_CAP_GIB) of Strata rc2 + Gyro: HIP, gfx1100 only, with the
rem unit tests.  Separate build and dist folders: build-hip-win and dist (run-gyro-m.bat) stay as they are.
title Strata HIP build gfx1100 - RAM ceiling
set "STRATA_HIP_ARCHS=gfx1100"
set "STRATA_GGML_DIR=E:\agention-llama.cpp"
set "BUILD_DIR=E:\Strata-gyro\build-cap"
set "DIST_DIR=E:\Strata-gyro\dist-cap"
set "PIP_CACHE_DIR=E:\pip-cache"
set "TMP=E:\_temp"
set "TEMP=E:\_temp"
set "PATH=C:\Users\Kirill\miniconda3\Scripts;C:\Users\Kirill\miniconda3;C:\Windows\System32;C:\Windows;%PATH%"
cd /d "E:\Strata-gyro"
echo ==== %DATE% %TIME% ==== >> "E:\Strata-gyro\build-cap.log"
call "E:\Strata-gyro\tools\hip\build_windows.bat" tests >> "E:\Strata-gyro\build-cap.log" 2>&1
set "RC=%ERRORLEVEL%"
echo EXITCODE=%RC% >> "E:\Strata-gyro\build-cap.log"
if "%RC%"=="0" (
  >"E:\Strata-gyro\dist-cap\COMMIT.txt" echo branch f-143-memlog, base
  git -C "E:\Strata-gyro" rev-parse HEAD >> "E:\Strata-gyro\dist-cap\COMMIT.txt"
  >>"E:\Strata-gyro\dist-cap\COMMIT.txt" echo plus the uncommitted memlog + STRATA_RAM_CAP_GIB port (docs\adr\ADR-0004-ram-cap-gyro.md^)
)
exit /b %RC%
