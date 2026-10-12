@echo off
rem Build Strata rc2's HIP engine (gfx1100 only) against the agentionai llama.cpp fork (trellis types).
title Strata HIP build gfx1100
set "STRATA_HIP_ARCHS=gfx1100"
set "STRATA_GGML_DIR=E:\agention-llama.cpp"
set "PIP_CACHE_DIR=E:\pip-cache"
set "TMP=E:\_temp"
set "TEMP=E:\_temp"
set "PATH=C:\Users\Kirill\miniconda3\Scripts;C:\Users\Kirill\miniconda3;C:\Windows\System32;C:\Windows;%PATH%"
cd /d "E:\Strata-gyro"
echo ==== %DATE% %TIME% ==== >> "E:\Strata-gyro\build-hip.log"
call "E:\Strata-gyro\tools\hip\build_windows.bat" >> "E:\Strata-gyro\build-hip.log" 2>&1
echo EXITCODE=%ERRORLEVEL% >> "E:\Strata-gyro\build-hip.log"
