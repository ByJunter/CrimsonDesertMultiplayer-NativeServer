# cdmp: para el servidor de consultas y comprueba que su proceso Java (y los procesos de su
# instancia) terminan. Si Java no sale en $TimeoutSec, se fuerza solo ese PID, después de comprobar
# que es el java.exe de la instancia que respondió al ping.
# Opcional: -Instance <id> para parar solo si el servidor que responde es esa instancia.
param([int]$TimeoutSec = 60, [ValidatePattern('^([0-9a-f]{32})?$')][string]$Instance = '')
. (Join-Path $PSScriptRoot '_env.ps1')
$ping = Get-CdmpPing
if (-not $ping) {
	# Un servidor propio colgado no responde al ping: con -Instance se paran sus procesos por el id
	# (solo los que llevan ese id de 32 hex en la línea de comandos).
	if ($Instance) {
		$own = @(Get-CdmpInstanceProcesses $Instance)
		foreach ($q in $own) { Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue }
		Start-Sleep -Seconds 2
		$still = @(Get-CdmpInstanceProcesses $Instance)
		"No responde ningún servidor; procesos de la instancia $Instance parados: $($own.Count), quedan: $($still.Count)"
		if ($still.Count) { "PARADO=incompleto"; exit 1 }
		"PARADO=$(if ($own.Count) { 'si-por-instancia' } else { 'no-habia' })"; return
	}
	"No hay servidor en ${CdmpHost}:$CdmpPort"; "PARADO=no-habia"; return
}
# El id viene de quien respondió al ping: solo se usa si tiene el formato propio.
if ($ping.Instance -notmatch '^[0-9a-f]{32}\z') { "El servidor que responde no da un id de instancia válido; no se para."; "PARADO=desconocido"; exit 1 }
if ($Instance -and $ping.Instance -ne $Instance) {
	"El servidor que responde es de otra instancia ($($ping.Instance)); no se para."; "PARADO=otra-instancia"; exit 1
}
$javaPid = $ping.Pid; $inst = $ping.Instance
$bye = Send-CdmpServer @('shutdown') -ReadTimeoutMs 30000
$proc = Get-Process -Id $javaPid -ErrorAction SilentlyContinue
$exited = $true
if ($proc) { $exited = $proc.WaitForExit($TimeoutSec * 1000) }
$forced = $false
if (-not $exited) {
	$ci = Get-CimInstance Win32_Process -Filter "ProcessId=$javaPid"
	if ($ci -and $ci.Name -eq 'java.exe' -and $ci.CommandLine -and $ci.CommandLine.Contains($inst) -and
		$ci.CommandLine.Contains('Cdmp_Query')) {
		Stop-Process -Id $javaPid -Force; $forced = $true
		Start-Sleep -Seconds 2
	}
}
$javaAlive = [bool](Get-Process -Id $javaPid -ErrorAction SilentlyContinue)
# Los procesos envolventes (powershell/cmd de la instancia) salen cuando Java termina.
$left = @()
if ($inst -match '^[0-9a-f]{32}$') {
	for ($i = 0; $i -lt 20; $i++) { $left = @(Get-CdmpInstanceProcesses $inst); if ($left.Count -eq 0) { break }; Start-Sleep -Milliseconds 500 }
}
"Servidor: respuesta a shutdown='$(([string]$bye).Trim())'; Java pid $javaPid " + $(if ($javaAlive) { 'SIGUE VIVO' } elseif ($forced) { 'terminado a la fuerza' } else { 'terminado' }) +
	"; procesos de la instancia que quedan: $($left.Count)"
foreach ($q in $left) { "  queda: pid $($q.ProcessId) $($q.Name)" }
if ($javaAlive -or $left.Count) { "PARADO=incompleto"; exit 1 } else { "PARADO=si" }
