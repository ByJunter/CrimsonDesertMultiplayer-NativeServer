# cdmp: vtable — métodos de la tabla virtual que empieza en esa dirección. Ej.: .\vtable.ps1 0x57260C0
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') vtable @args
