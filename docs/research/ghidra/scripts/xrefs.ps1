# cdmp: xrefs — quién llama a esa función o lee/escribe esa dirección. Ej.: .\xrefs.ps1 0x1317D40
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') xrefs @args
