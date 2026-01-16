$in = $false
$keep = $true
$src = "main.cpp"
$bak = "$src.bak"
if (-not (Test-Path $src)) { Write-Error "$src not found"; exit 1 }
$lines = Get-Content $src -Encoding UTF8
Set-Content -Path $bak -Value $lines -Encoding UTF8
$out = @()
Get-Content $bak -Encoding UTF8 | ForEach-Object {
    $line = $_
    if ($line -match '^<<<<<<<') { $in = $true; $keep = $true; return }
    if ($in -and $line -match '^=======$') { $keep = $false; return }
    if ($in -and $line -match '^>>>>>>>') { $in = $false; $keep = $true; return }
    if (-not $in) { $out += $line } else { if ($keep) { $out += $line } }
}
$out | Set-Content $src -Encoding UTF8
Write-Output "Conflict markers removed (kept HEAD blocks). Backup saved to $bak" 
