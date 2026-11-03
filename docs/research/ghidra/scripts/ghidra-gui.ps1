# cdmp: abre la interfaz gráfica de Ghidra con el JDK de D:\Tools (proyecto: File > Open Project >
# D:\RE\CrimsonDesert_2976\project\CrimsonDesert_2976.gpr). Para el servidor antes (server-stop.ps1):
# el proyecto no se puede abrir en dos procesos a la vez.
. (Join-Path $PSScriptRoot '_env.ps1')
Start-Process -FilePath (Join-Path $CdmpGhidra 'ghidraRun.bat') -WorkingDirectory $CdmpGhidra
