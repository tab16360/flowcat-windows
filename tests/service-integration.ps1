# Exercises only FlowCat's installed service and retains its history throughout.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cli=Join-Path $root 'build\Release\FlowCatCli.exe'
$script:checks=0
function Check($value,[string]$message){if(-not $value){throw "FAIL: $message"};$script:checks++;"PASS: $message"}
function Snapshot {
 $out=Join-Path $root 'build\integration-snapshot.json'
 & $cli --out $out
 Get-Content $out -Raw -Encoding UTF8 | ConvertFrom-Json
}
function Ready {
 for($i=0;$i -lt 20;$i++){ $s=Snapshot;if($s.ok){return $s};Start-Sleep -Milliseconds 500 }
 throw 'Service did not become ready'
}
& "$PSScriptRoot\desktop.ps1" -Action Stop
$s=Ready
& $cli --resume | Out-Null
Start-Sleep 3
$s=Snapshot
Check ($s.ok -and $s.error -eq 0 -and $s.saved) 'installed service samples without UI'
$before=$s.events;Start-Sleep 4;$s=Snapshot
Check ($s.events -gt $before) 'sampling continues after tray exits'
& $cli --pause | Out-Null
Start-Sleep 4;$a=Snapshot;Start-Sleep 4;$b=Snapshot
Check ($a.paused -and $b.paused -and $a.events -eq $b.events) 'pause stops event accumulation'
Stop-Service FlowCatCollector
Start-Service FlowCatCollector
$s=Ready
Check $s.paused 'paused setting survives service restart'
Check ($s.rows.Count -gt 0) 'history survives service restart'
& $cli --invalid | Out-Null
Check ($LASTEXITCODE -ne 0 -and (Snapshot).ok) 'invalid IPC protocol rejected without stopping service'
$filtered=Join-Path $root 'build\integration-filter.json'
& $cli --network '__FlowCat_nonexistent_test_network__' --out $filtered
$none=Get-Content $filtered -Raw -Encoding UTF8 | ConvertFrom-Json
Check ($none.rows.Count -eq 0 -and $none.physical.down -eq 0 -and $none.physical.up -eq 0) 'network filter excludes other SSIDs'
& $cli --resume | Out-Null
Start-Sleep 4;$s=Snapshot
Check (-not $s.paused -and $s.events -gt 0) 'service resumes without a tray process'
& $cli --burst --out "$root\build\burst.json"
$burst=Get-Content "$root\build\burst.json" -Raw -Encoding UTF8 | ConvertFrom-Json
Check ($burst.ok -and $burst.completed -eq 100) '100 consecutive IPC exchanges complete without dropped replies'
& $cli --wifi-off | Out-Null
Start-Sleep 3;$s=Snapshot
Check (-not $s.identifyNetworks -and $s.network -eq ([string][char]0x5168+[char]0x90E8+[char]0x7F51+[char]0x7EDC)) 'Wi-Fi opt-out records only all networks'
& $cli --wifi-on | Out-Null
Start-Sleep 3;$s=Snapshot
Check $s.identifyNetworks 'Wi-Fi identification can be re-enabled'
"All $script:checks service integration checks passed."
