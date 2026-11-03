# cdmp: front-end común de las consultas.  Uso: cdmp-query.ps1 <comando> <arg1> [<arg2>]
#  Comandos: decomp, xrefs, strings, vtable, class, imports, func, disasm, symbols  (ver Cdmp_Query.java).
#  Cada argumento de PowerShell es un campo (un nombre con espacios va entre comillas). Ningún
#  argumento llega a una línea de cmd: con servidor van por el socket (campos separados por TAB);
#  sin servidor van en un fichero de petición que lee Cdmp_Query.java (analyzeHeadless -readOnly).
param([Parameter(Mandatory)][string]$Command, [Parameter(ValueFromRemainingArguments)][string[]]$Rest)
. (Join-Path $PSScriptRoot '_env.ps1')
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$fields = @($Command) + @($Rest | Where-Object { $_ -ne $null })
Assert-CdmpFields $fields
$sw = [Diagnostics.Stopwatch]::StartNew()
$res = Send-CdmpServer $fields
$mode = 'servidor'
if ($null -eq $res) {
	$mode = 'headless'
	# Las consultas no necesitan los 22 GB del análisis.
	if (-not $env:GHIDRA_HEADLESS_MAXMEM) { $env:GHIDRA_HEADLESS_MAXMEM = '12G' }
	$id = [guid]::NewGuid().ToString('N')
	$req = Join-Path $env:TEMP "cdmp_req_$id.txt"
	$out = Join-Path $env:TEMP "cdmp_out_$id.txt"
	[IO.File]::WriteAllText($req, ($fields -join "`t"), (New-Object System.Text.UTF8Encoding($false)))
	try {
		$log = Invoke-CdmpHeadless -HArgs @('-process', $CdmpProgram, '-noanalysis', '-readOnly', '-scriptPath', $PSScriptRoot,
			'-postScript', 'Cdmp_Query.java', "req=$req", "out=$out", "decompdir=$(Join-Path $CdmpRoot 'exports\decomp')")
		if (Test-Path $out) { $res = [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8) }
		else { $res = "ERROR: Ghidra no devolvió resultado.`n" + (($log | Select-String 'ERROR|Exception|lock' | Select-Object -First 15) -join "`n") }
	} finally {
		Remove-Item $req, $out -ErrorAction SilentlyContinue
	}
}
$sw.Stop()
$res
"# total {0:N2} s ({1})" -f $sw.Elapsed.TotalSeconds, $mode
