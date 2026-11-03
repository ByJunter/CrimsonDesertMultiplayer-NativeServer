# cdmp: symbols — símbolos con nombre (no FUN_/DAT_) que contienen el texto. Ej.: .\symbols.ps1 NwVirtualAsyncSession
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') symbols @args
