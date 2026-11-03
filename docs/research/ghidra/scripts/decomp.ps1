# cdmp: decomp — pseudocódigo C de la función (guardado en exports\decomp\<rva>.c). Ej.: .\decomp.ps1 0x35CA00  |  .\decomp.ps1 ClientNet::OnRecvPacket
# Usa el servidor si está levantado (server-start.ps1); si no, Ghidra headless -readOnly.
& (Join-Path $PSScriptRoot 'cdmp-query.ps1') decomp @args
