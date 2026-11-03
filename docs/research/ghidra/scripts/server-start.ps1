# cdmp: levanta el servidor de consultas (Ghidra headless, -readOnly) en 127.0.0.1:$CdmpPort,
# como proceso oculto independiente de esta consola. Tarda lo que tarde en abrir el proyecto
# (~12 s); después cada consulta responde en < 1 s. Mientras está levantado, el proyecto queda
# bloqueado para otros procesos de Ghidra. Pararlo: server-stop.ps1.
#
# Identidad: cada arranque lleva un id de instancia aleatorio (32 hex) que viaja en la línea de
# comandos de sus procesos y que el servidor devuelve en "ping" junto con su PID de Java. Solo se da
# por bueno un servidor que responda con ese id. Si el arranque falla, se matan los procesos que
# llevan ese id (y nada más). Escribe la identidad en la salida: "PID_JAVA=<n>" e "INSTANCIA=<id>".
param([int]$TimeoutSec = 900, [ValidatePattern('^([0-9a-f]{32})?$')][string]$Instance = '')
. (Join-Path $PSScriptRoot '_env.ps1')
$cur = Get-CdmpPing
if ($cur) {
	"Ya hay un servidor en ${CdmpHost}:$CdmpPort (pid Java $($cur.Pid), instancia $($cur.Instance)); no se arranca otro."
	"PID_JAVA=$($cur.Pid)"; "INSTANCIA=$($cur.Instance)"; "PROPIO=no"
	return
}
if (-not $Instance) { $Instance = New-CdmpInstanceId }
$p = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass',
	'-File', (Join-Path $PSScriptRoot 'server-run.ps1'), '-Instance', $Instance)
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
	Start-Sleep -Milliseconds 500
	$ping = Get-CdmpPing
	if ($ping) {
		if ($ping.Instance -ne $Instance) {
			"Responde en ${CdmpHost}:$CdmpPort un servidor de OTRA instancia ($($ping.Instance), pid $($ping.Pid)); el arranque propio se cancela."
			break
		}
		"Servidor listo en ${CdmpHost}:$CdmpPort tras {0:N1} s (pid Java {1}, instancia {2}, sha256 {3})" -f $sw.Elapsed.TotalSeconds, $ping.Pid, $Instance, $ping.Sha256
		"PID_JAVA=$($ping.Pid)"; "INSTANCIA=$Instance"; "PROPIO=si"
		return
	}
	if ($p.HasExited) { "El servidor terminó sin arrancar; mira $(Join-Path $CdmpRoot 'logs\server.log')"; break }
}
if ($sw.Elapsed.TotalSeconds -ge $TimeoutSec) { "El servidor no respondió en $TimeoutSec s; mira $(Join-Path $CdmpRoot 'logs\server.log')" }
# Arranque fallido: se limpian solo los procesos de esta instancia.
$left = @(Get-CdmpInstanceProcesses $Instance)
foreach ($q in $left) { Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
$still = @(Get-CdmpInstanceProcesses $Instance)
"Limpieza del arranque fallido: $($left.Count) procesos de la instancia; quedan $($still.Count)."
"PID_JAVA="; "INSTANCIA=$Instance"; "PROPIO=fallo"
exit 1
