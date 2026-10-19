$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cli=Join-Path $root 'build\Release\FlowCatCli.exe'
$app=Join-Path $env:ProgramFiles 'FlowCat Windows'
$iscc="${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
& "$PSScriptRoot\desktop.ps1" -Action Stop
& $cli --pause | Out-Null
$before=(Get-FileHash "$app\FlowCatService.exe" -Algorithm SHA256).Hash
& $iscc "/DServiceBinary=$root\build\Release\FlowCatFailService.exe" '/DSetupName=FlowCat-Intentional-Failure' "$root\installer\FlowCat.iss" | Out-Null
if($LASTEXITCODE){throw 'Failed to build intentional failure fixture'}
$p=Start-Process "$root\dist\FlowCat-Intentional-Failure.exe" -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOAUTOSTART=1 /LOG=$root\build\rollback.log" -Wait -PassThru
if($p.ExitCode -eq 0){throw 'Broken collector was incorrectly accepted'}
'PASS: failed collector installation is rejected'
if((Get-FileHash "$app\FlowCatService.exe" -Algorithm SHA256).Hash -ne $before){throw 'Previous collector was not restored'}
'PASS: previous collector binary restored byte-for-byte'
& $cli --out "$root\build\rollback-snapshot.json"
$s=Get-Content "$root\build\rollback-snapshot.json" -Raw -Encoding UTF8 | ConvertFrom-Json
if(-not $s.ok -or -not $s.saved -or (Get-Service FlowCatCollector).Status -ne 'Running'){throw 'Restored service is not healthy'}
'PASS: restored service starts and reads retained history'
$p=Start-Process "$root\dist\FlowCat-Windows-x64-Setup.exe" -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOAUTOSTART=1' -Wait -PassThru
if($p.ExitCode){throw 'Failed to restore normal installer configuration'}
& $cli --resume | Out-Null
'All 3 rollback checks passed.'
