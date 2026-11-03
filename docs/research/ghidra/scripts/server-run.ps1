# cdmp: ejecuta el servidor de consultas en primer plano (lo lanza server-start.ps1).
param([Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{32}$')][string]$Instance)
. (Join-Path $PSScriptRoot '_env.ps1')
$log = Join-Path $CdmpRoot 'logs\server.log'
"===== servidor $(Get-Date -Format o) proyecto=$CdmpProject\$CdmpProjName programa=$CdmpProgram ${CdmpHost}:$CdmpPort instancia=$Instance" |
	Out-File -Append -Encoding utf8 $log
# Menos heap que el análisis: las consultas no necesitan 22 GB.
if (-not $env:GHIDRA_HEADLESS_MAXMEM) { $env:GHIDRA_HEADLESS_MAXMEM = '12G' }
try {
	Invoke-CdmpHeadless -HArgs @('-process', $CdmpProgram, '-noanalysis', '-readOnly', '-scriptPath', $PSScriptRoot,
		'-postScript', 'Cdmp_Query.java', "decompdir=$(Join-Path $CdmpRoot 'exports\decomp')", "serve=$CdmpPort",
		"instance=$Instance") -Log $log
} finally {
	"===== servidor parado $(Get-Date -Format o) instancia=$Instance" | Out-File -Append -Encoding utf8 $log
}
