# cdmp: class — tablas virtuales de las clases RTTI cuyo nombre coincide. Ej.: .\class.ps1 NwVirtualAsyncSession
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') class @args
