# cdmp: strings — cadenas que contienen el texto y funciones que las usan. Ej.: .\strings.ps1 sentry
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') strings @args
