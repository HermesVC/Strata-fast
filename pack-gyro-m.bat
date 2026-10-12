@echo off
rem Pack Gyro-M: dense weights + tokenizer + Hadamard/expert tables (experts and the PLE table stay in the GGUF).
title Gyro-M pack
set "STRATA_GGUF_PY=E:\agention-llama.cpp\gguf-py"
set "PIP_CACHE_DIR=E:\pip-cache"
cd /d "E:\Strata-gyro"
"C:\Users\Kirill\miniconda3\python.exe" -u "E:\Strata-gyro\tools\iq_pack.py" ^
  --gguf "E:\Strata-data\models\GYRO_M\Qwen3.8-Flash-Next-Gyro-M-TQ2_0.gguf" ^
  --out "E:\Strata-data\packs\gyro-m" --compat-bf16 >> "E:\Strata-gyro\pack-gyro-m.log" 2>&1
echo EXITCODE=%ERRORLEVEL% >> "E:\Strata-gyro\pack-gyro-m.log"
