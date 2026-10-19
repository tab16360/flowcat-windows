param([ValidateSet('Launch','Capture','Query','WriteProbe','Stop','FullExit','StartupOff','StartupOn')][string]$Action='Launch')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $env:ProgramFiles 'FlowCat Windows\FlowCat.exe'
$args='--show'
if($Action -eq 'Capture'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args=Join-Path $root 'build'}
if($Action -eq 'FullExit'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--full-exit'}
if($Action -eq 'StartupOff'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--startup-off'}
if($Action -eq 'StartupOn'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--startup-on'}
if($Action -eq 'Stop'){$exe=Join-Path $root 'build\Release\FlowCatUiCapture.exe';$args='--stop'}
if($Action -eq 'WriteProbe'){$exe=Join-Path $root 'build\Release\FlowCatCli.exe';$args="--write-probe --out $root\build\limited-write-probe.json"}
if($Action -eq 'Query'){$exe=Join-Path $root 'build\Release\FlowCatCli.exe';$args="--out $root\build\limited-client.json"}
$name='FlowCat-Test-'+[guid]::NewGuid().ToString()
$a=New-ScheduledTaskAction -Execute $exe -Argument $args
$p=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
$s=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $name -Action $a -Principal $p -Settings $s | Out-Null
try {
 Start-ScheduledTask $name
 Start-Sleep 2
 if($Action -eq 'Launch'){
  Start-Sleep 2
  if(-not(Get-Process FlowCat -ErrorAction SilentlyContinue | Where-Object {$_.SessionId -ne 0})){throw 'FlowCat failed to stay running in the interactive session'}
 }else{
  for($i=0;$i -lt 225;$i++){if((Get-ScheduledTask -TaskName $name).State -ne 'Running'){break};Start-Sleep -Milliseconds 200}
  $task=Get-ScheduledTask -TaskName $name
  $info=Get-ScheduledTaskInfo -TaskName $name
  if($task.State -eq 'Running' -or ($info.LastTaskResult -ne 0 -and -not($Action -eq 'Stop' -and $info.LastTaskResult -eq 2))){throw "Desktop action $Action failed: $($info.LastTaskResult)"}
 }
} finally {Unregister-ScheduledTask $name -Confirm:$false}
