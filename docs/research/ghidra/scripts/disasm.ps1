# cdmp: disasm — n instrucciones (40 por defecto) con bytes y referencias. Ej.: .\disasm.ps1 0x2356420 8
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') disasm @args
