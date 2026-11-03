# cdmp: func — entrada, tamaño, firma, nombres, llamadores y llamados. Ej.: .\func.ps1 0x2356270
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') func @args
