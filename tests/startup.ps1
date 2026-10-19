$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$originalRun=[bool]([Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run','FlowCatWindows',$null))
$originalService=(Get-Service FlowCatCollector).StartType -eq 'Automatic'
if($originalRun -ne $originalService){throw 'Normalize tray and service startup preferences before running this combined startup test.'}
try {
& "$root\tests\desktop.ps1" -Action StartupOff
if((Get-Service FlowCatCollector).StartType -ne 'Manual'){throw 'Disabling startup did not change service to Manual'}
if([Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run','FlowCatWindows',$null)){throw 'Tray startup was not removed'}
'PASS: startup off disables tray and boot collector'
& "$root\tests\desktop.ps1" -Action Stop
$p=Start-Process "$root\dist\FlowCat-Windows-x64-Setup.exe" -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOAUTOSTART=1' -Wait -PassThru
if($p.ExitCode){throw "Upgrade failed $($p.ExitCode)"}
if((Get-Service FlowCatCollector).StartType -ne 'Manual'){throw 'Upgrade lost disabled startup setting'}
'PASS: upgrade preserves disabled boot collector'
& "$root\tests\desktop.ps1" -Action Launch
if([Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run','FlowCatWindows',$null)){throw 'App launch lost disabled startup preference'}
'PASS: app launch preserves disabled tray startup'
& "$root\tests\desktop.ps1" -Action StartupOn
if((Get-Service FlowCatCollector).StartType -ne 'Automatic'){throw 'Enabling startup did not restore boot collector'}
if(-not([Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run','FlowCatWindows',$null))){throw 'Tray startup was not restored'}
'PASS: enabling startup restores both mechanisms'

} finally {if($originalRun){& "$root\tests\desktop.ps1" -Action StartupOn}else{& "$root\tests\desktop.ps1" -Action StartupOff}}
