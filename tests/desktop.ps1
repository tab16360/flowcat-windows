param([ValidateSet('Launch','Capture','Query','WriteProbe','Stop','FullExit')][string]$Action='Launch')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $env:ProgramFiles 'FlowCat Windows\FlowCat.exe'
$args='--show'
if($Action -eq 'Capture'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args=Join-Path $root 'build'}
if($Action -eq 'FullExit'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--full-exit'}
if($Action -eq 'Stop'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--stop'}
if($Action -eq 'WriteProbe'){$exe=Join-Path $root 'build\Release\FlowCatCli.exe';$args="--write-probe --out $root\build\limited-write-probe.json"}
if($Action -eq 'Query'){$exe=Join-Path $root 'build\Release\FlowCatCli.exe';$args="--out $root\build\limited-client.json"}
$name='FlowCat-Test-'+[guid]::NewGuid().ToString()
$a=New-ScheduledTaskAction -Execute $exe -Argument $args
$p=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
$s=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $name -Action $a -Principal $p -Settings $s | Out-Null
try {Start-ScheduledTask $name;Start-Sleep 4} finally {Unregister-ScheduledTask $name -Confirm:$false}
