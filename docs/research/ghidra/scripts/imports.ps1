# cdmp: imports — dónde se llama a una importación. Ej.: .\imports.ps1 sentry_capture_event_with_scope
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') imports @args
