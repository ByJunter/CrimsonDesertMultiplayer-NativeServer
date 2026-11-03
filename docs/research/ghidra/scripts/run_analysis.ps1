# cdmp: análisis automático completo del proyecto ya importado (puede durar horas).
# Uso:  powershell -File D:\RE\CrimsonDesert_2976\scripts\run_analysis.ps1 [-MaxCpu 10] [-Disable "A;B"]
#  -Disable: analizadores extra que se desactivan (p. ej. "Decompiler Switch Analysis"); pasan por
#  una variable de entorno, no por la línea de cmd.
# Reanudar: si se interrumpe, el proyecto queda como estaba al último guardado (Ghidra en headless
# solo guarda al terminar); basta con volver a ejecutar este script. Para el servidor antes.
param([ValidateRange(1, 64)][int]$MaxCpu = 10, [string]$Disable = '')
. (Join-Path $PSScriptRoot '_env.ps1')
$log = Join-Path $CdmpRoot 'logs\analysis.log'
if ($Disable) { $env:CDMP_DISABLE_ANALYZERS = $Disable }
$t0 = Get-Date
"===== INICIO $(Get-Date -Format o)  max-cpu=$MaxCpu disable='$Disable'" | Out-File -Append -Encoding utf8 $log
Invoke-CdmpHeadless -HArgs @('-process', $CdmpProgram, '-scriptPath', $PSScriptRoot, '-preScript', 'Cdmp_PreAnalysis.java',
	'-postScript', 'Cdmp_Inspect.java', '-max-cpu', "$MaxCpu") -Log $log -Priority LOW
$rc = $LASTEXITCODE
$d = (Get-Date) - $t0
"===== FIN $(Get-Date -Format o)  duracion=$($d.ToString('hh\:mm\:ss'))  rc=$rc" | Out-File -Append -Encoding utf8 $log
