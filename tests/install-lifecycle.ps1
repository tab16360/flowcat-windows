param([switch]$AllowUninstallTest)
$ErrorActionPreference='Stop'
if(-not $AllowUninstallTest){throw 'Pass -AllowUninstallTest to test uninstall/reinstall while retaining FlowCat history.'}
$root=Split-Path $PSScriptRoot -Parent
$setup=Join-Path $root 'dist\FlowCat-Windows-x64-Setup.exe'
$cli=Join-Path $root 'build\Release\FlowCatCli.exe'
$app=Join-Path $env:ProgramFiles 'FlowCat Windows'
$db=Join-Path $env:ProgramData 'FlowCatWindows\history.sqlite'
$script:checks=0
function Check($good,[string]$name){if(-not $good){throw "FAIL: $name"};$script:checks++;"PASS: $name"}
function Snap { & $cli --out "$root\build\lifecycle.json";Get-Content "$root\build\lifecycle.json" -Raw -Encoding UTF8 | ConvertFrom-Json }
function RunSetup([string]$extra=''){$p=Start-Process $setup -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOAUTOSTART=1 $extra" -Wait -PassThru;Check ($p.ExitCode -eq 0) 'installer completed successfully'}
& "$PSScriptRoot\desktop.ps1" -Action Stop
& $cli --wifi-off | Out-Null
& $cli --pause | Out-Null
Start-Sleep 3
$before=Snap
Stop-Service FlowCatCollector
$hash=(Get-FileHash $db -Algorithm SHA256).Hash
$p=Start-Process (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait -PassThru
Check ($p.ExitCode -eq 0 -and -not (Get-Service FlowCatCollector -ErrorAction SilentlyContinue)) 'uninstall removes the service'
Check ((Test-Path $db) -and (Get-FileHash $db -Algorithm SHA256).Hash -eq $hash) 'uninstall retains database byte-for-byte'
$runKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run');$run=$null;if($runKey){$run=$runKey.GetValue('FlowCatWindows',$null);$runKey.Dispose()}
Check (-not $run) 'uninstall removes the login startup entry'
RunSetup
Start-Sleep 2;$after=Snap
Check ($after.ok -and $after.rows.Count -ge $before.rows.Count) 'reinstall recovers preserved history'
Check (-not $after.identifyNetworks) 'Wi-Fi privacy opt-out survives uninstall and reinstall'
RunSetup '/UPDATE=1'
$after=Snap
Check ($after.ok -and (Get-Service FlowCatCollector).Status -eq 'Running') 'upgrade restarts the collector'
Check ((Test-Path "$app\backup\FlowCat.exe") -and (Test-Path "$app\backup\FlowCatService.exe")) 'upgrade retains previous binaries for rollback'
& $cli --wifi-on | Out-Null
& $cli --resume | Out-Null
"All $script:checks install lifecycle checks passed."
