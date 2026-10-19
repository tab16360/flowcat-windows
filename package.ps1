param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
if(-not $SkipBuild){& "$PSScriptRoot\build.ps1";if($LASTEXITCODE){exit $LASTEXITCODE}}
$iscc="${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if(-not(Test-Path $iscc)){$iscc="${env:ProgramFiles}\Inno Setup 6\ISCC.exe"}
if(-not(Test-Path $iscc)){throw 'Install Inno Setup 6 before packaging.'}
& $iscc "$PSScriptRoot\installer\FlowCat.iss"
if($LASTEXITCODE){exit $LASTEXITCODE}
$installer=Join-Path $PSScriptRoot 'dist\FlowCat-Windows-x64-Setup.exe'
$hash=(Get-FileHash $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  FlowCat-Windows-x64-Setup.exe" | Set-Content "$installer.sha256" -Encoding ascii
Get-Item $installer | Select-Object Name,Length
