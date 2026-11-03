# cdmp: entorno común de las herramientas de Ghidra (solo para este proceso; no toca el sistema).
$global:CdmpRoot    = 'D:\RE\CrimsonDesert_2976'
$global:CdmpGhidra  = 'D:\Tools\ghidra_12.1.4_PUBLIC'
$global:CdmpJdk     = 'D:\Tools\jdk-21.0.12.1+1'
$global:CdmpProject = Join-Path $CdmpRoot 'project'
$global:CdmpProjName = 'CrimsonDesert_2976'
$global:CdmpProgram = 'CrimsonDesert.exe'
$global:CdmpBase    = [UInt64]0x140000000
$global:CdmpHost    = '127.0.0.1'    # servidor de consultas: solo esta dirección
$global:CdmpPort    = 18490
# Para probar las herramientas con otro proyecto (p. ej. uno pequeño):
if ($env:CDMP_PROJECT_DIR)  { $global:CdmpProject  = $env:CDMP_PROJECT_DIR }
if ($env:CDMP_PROJECT_NAME) { $global:CdmpProjName = $env:CDMP_PROJECT_NAME }
if ($env:CDMP_PROGRAM)      { $global:CdmpProgram  = $env:CDMP_PROGRAM }
if ($env:CDMP_PORT) {
	$p = 0
	if (-not [int]::TryParse($env:CDMP_PORT, [ref]$p) -or $p -lt 1024 -or $p -gt 65535) { throw "CDMP_PORT inválido" }
	$global:CdmpPort = $p
}
$env:JAVA_HOME = $CdmpJdk
$env:PATH = (Join-Path $CdmpJdk 'bin') + ';' + $env:PATH

# Solo letras, dígitos y _ : \ . / = + -  (sin espacios, comillas, %, ^, &, |, <, >, !, paréntesis…):
# todo lo que llega a la línea de cmd de analyzeHeadless tiene que pasar este filtro, así que
# ningún argumento puede convertirse en sintaxis de cmd. Las consultas NO van por aquí: van en un
# fichero de petición (cdmp-query.ps1) o por el socket (Send-CdmpServer).
$global:CdmpSafeArg = '^[A-Za-z0-9_:\\./=+\-]+\z'  # \z: "$" would also accept a trailing newline
function Assert-CdmpSafeArg([string]$Value, [string]$What) {
	if ($null -eq $Value -or $Value -notmatch $CdmpSafeArg) { throw "Argumento no permitido para la línea de cmd ($What): '$Value'" }
}

# Ejecuta analyzeHeadless sobre el proyecto existente con prioridad baja.
#  $HArgs: argumentos tras "<proyecto> <nombre>" (todos deben pasar Assert-CdmpSafeArg).
#  La salida va a $Log (si se da) o se devuelve.
function Invoke-CdmpHeadless {
	param([string[]]$HArgs, [string]$Log, [ValidateSet('LOW', 'BELOWNORMAL')][string]$Priority = 'BELOWNORMAL')
	$bat = Join-Path $CdmpGhidra 'support\analyzeHeadless.bat'
	Assert-CdmpSafeArg $bat 'analyzeHeadless.bat'
	Assert-CdmpSafeArg $CdmpProject 'proyecto'
	Assert-CdmpSafeArg $CdmpProjName 'nombre del proyecto'
	foreach ($a in $HArgs) { Assert-CdmpSafeArg $a 'argumento' }
	if ($Log) { Assert-CdmpSafeArg $Log 'log' }
	# "start <bat>" usaría "cmd /K" (que se queda abierto si hay consola): por eso "cmd /c <bat>".
	# Todos los argumentos van entre comillas (cmd parte por '=' fuera de ellas). Es seguro porque el
	# filtro de arriba impide comillas y '%' dentro. El .bat va sin comillas: si la línea de "cmd /c"
	# empezara por comilla, cmd quitaría la primera y la última.
	$q = { param($s) '"' + $s + '"' }
	$cmdline = "start `"`" /$Priority /B /WAIT cmd.exe /d /c $bat " + (& $q $CdmpProject) + ' ' + (& $q $CdmpProjName) + ' ' +
		(($HArgs | ForEach-Object { & $q $_ }) -join ' ')
	if ($Log) { $cmdline += ' >> ' + (& $q $Log) + ' 2>&1' } else { $cmdline += ' 2>&1' }
	# Un .cmd temporal evita las reglas de comillas de "cmd /c" desde PowerShell 5.1.
	$tmp = Join-Path $env:TEMP ("cdmp_h_{0}.cmd" -f [guid]::NewGuid().ToString('N'))
	Set-Content -Path $tmp -Value "@echo off`r`n$cmdline`r`n" -Encoding ASCII
	try { & cmd.exe /d /c $tmp } finally { Remove-Item $tmp -ErrorAction SilentlyContinue }
}

# Valida los campos de una consulta (sin caracteres de control ni TAB, <= 512 caracteres).
function Assert-CdmpFields([string[]]$Fields) {
	if (-not $Fields -or $Fields.Count -lt 1 -or $Fields.Count -gt 3) { throw "Una consulta tiene de 1 a 3 campos" }
	foreach ($f in $Fields) {
		if ([string]::IsNullOrEmpty($f)) { throw "Campo vacío" }
		if ($f.Length -gt 512) { throw "Campo de más de 512 caracteres" }
		if ($f -match '[\x00-\x1F\x7F]') { throw "Campo con caracteres de control" }
	}
}

# Envía una consulta estructurada al servidor (campos separados por TAB); $null si no hay servidor.
function Send-CdmpServer {
	param([string[]]$Fields, [int]$ConnectMs = 300, [int]$ReadTimeoutMs = 180000, [int]$MaxBytes = 2MB)
	Assert-CdmpFields $Fields
	$c = New-Object System.Net.Sockets.TcpClient
	try {
		$ar = $c.BeginConnect($CdmpHost, $CdmpPort, $null, $null)
		if (-not $ar.AsyncWaitHandle.WaitOne($ConnectMs)) { return $null }
		$c.EndConnect($ar)
		$c.ReceiveTimeout = $ReadTimeoutMs
		$c.SendTimeout = 10000
		$s = $c.GetStream()
		$b = [System.Text.Encoding]::UTF8.GetBytes(($Fields -join "`t") + "`n")
		$s.Write($b, 0, $b.Length); $s.Flush()
		$ms = New-Object System.IO.MemoryStream
		$buf = New-Object byte[] 65536
		while (($n = $s.Read($buf, 0, $buf.Length)) -gt 0) {
			$ms.Write($buf, 0, $n)
			if ($ms.Length -ge $MaxBytes) { break }
		}
		$txt = [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
		if ($ms.Length -ge $MaxBytes) { $txt += "`n... (cortado en $MaxBytes bytes por el cliente)`n" }
		return $txt
	} catch { return $null } finally { $c.Close() }
}

# Identidad del servidor que responde: @{Program; Sha256; Pid; Instance} o $null.
function Get-CdmpPing {
	$r = Send-CdmpServer @('ping') -ReadTimeoutMs 10000
	if ($null -eq $r) { return $null }
	$l = ($r -split "`n")[0].TrimEnd("`r")
	$p = $l -split "`t"
	if ($p.Count -lt 5 -or $p[0] -ne 'pong') { return $null }
	return [pscustomobject]@{ Program = $p[1]; Sha256 = $p[2]; Pid = [int]$p[3]; Instance = $p[4] }
}

# Procesos (powershell, cmd, java) lanzados para una instancia: su línea de comandos lleva el id
# (server-run.ps1 -Instance <id>, y java/cmd "instance=<id>"). El id son 32 hex aleatorios.
function Get-CdmpInstanceProcesses([string]$Instance) {
	if ($Instance -notmatch '^[0-9a-f]{32}\z') { throw "Instancia inválida" }
	Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and $_.CommandLine.Contains($Instance) }
}

function New-CdmpInstanceId { [guid]::NewGuid().ToString('N') }
