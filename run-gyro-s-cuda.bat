@echo off
rem Gyro-S on an RTX 5060 Ti (16 GB) with 32 GB of RAM. The engine is the one build-cuda.bat writes
rem (dist-cuda\COMMIT.txt says from what). Same speed choices as run-gyro-m-cap.bat: page-locked resident
rem copy, --pcie-frac 1.0, --spec 4 --spec-min-p 0.75, --kv k8v4, --prefill auto, 950 MiB left for the desktop,
rem STRATA_PREFILL_MMQ=0, STRATA_DECODE_TIMING=1.
rem
rem Gyro-S's experts are 24 GiB. At 200k context, k8v4, a 16 GB card keeps about 9 GiB of them; the other
rem ~15 GiB fit in 32 GB of RAM beside the engine's own reserve, so this is --resident-experts, not a file-cache
rem tier. Headroom is 4 GiB (the engine default). The 48 GB box uses 3; on 32 GB that extra gig is the OS.
rem The process ceiling defaults to 24 GiB, which leaves about 8 GB for Windows. A lower ceiling than the
rem resident copy drops the engine onto disk reads. If the start stalls while pinning, set STRATA_RESIDENT_PIN=0
rem and run this again (pageable copy; misses then match the mapped path).
rem Windows also charges every GPU allocation to the commit charge: the page file wants to be System managed.
title Strata Gyro-S CUDA - 5060 Ti 16 GB, 32 GB RAM
setlocal EnableExtensions
if not defined STRATA_RAM_CAP_GIB set "STRATA_RAM_CAP_GIB=24"
set "ROOT=%~dp0"
cd /d "%ROOT%"
set "PY=%ROOT%.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=E:\Strata\.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=C:\Users\Kirill\miniconda3\python.exe"
if not exist "%PY%" set "PY=python"
set "EXE=%ROOT%dist-cuda\strata-windows-x64-cuda\strata.exe"
set "GGUF=%ROOT%..\Strata-data\models\GYRO_S\Qwen3.8-Flash-Next-Gyro-S-TQ1_0.gguf"
set "PACK=%ROOT%..\Strata-data\packs\gyro-s"
if not exist "%EXE%" (echo missing engine: run build-cuda.bat first & pause & exit /b 1)
if not exist "%GGUF%" (echo missing Gyro-S: %GGUF% & echo The pack is made from that file. See docs\GYRO.md, model S. & pause & exit /b 1)
if not exist "%PACK%\tokenizer" (echo missing pack: %PACK% & echo Pack the GGUF with tools\iq_pack.py --compat-bf16 before starting. & pause & exit /b 1)
echo Engine: %EXE%
echo Config: %ROOT%strata-gyro-s-cuda.json
echo Model:  %GGUF%
echo Log:    %ROOT%strata-gyro-s-cuda.log
echo RAM ceiling: STRATA_RAM_CAP_GIB=%STRATA_RAM_CAP_GIB%   memory lines: STRATA_MEMLOG=%STRATA_MEMLOG%
echo Ready line: http://127.0.0.1:9798/v1
echo.
"%PY%" -m serve.server --engine strata --config "%ROOT%strata-gyro-s-cuda.json"
echo.
echo Stopped. exit=%ERRORLEVEL%
pause
