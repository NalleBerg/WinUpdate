param(
    [int]$stage = 0
)
# Update PROBE_STAGE in main.cpp
$src = Get-Content .\main.cpp -Raw
$src = [regex]::Replace($src, '(#ifndef PROBE_STAGE\s*#define PROBE_STAGE)\s*\d+', '${1} ' + $stage)
Set-Content -Path .\main.cpp -Value $src -Encoding UTF8
Write-Output "Wrote PROBE_STAGE=$stage to main.cpp"
# Build
cmake --build build --config Release --target WinUpdate | Out-Host
# Run for 3 seconds then kill
$p = Start-Process -FilePath (Resolve-Path .\build\WinUpdate.exe) -PassThru
Start-Sleep -Seconds 3
if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output "RunComplete (stage=$stage)"
