# Backup and prune script for WinUpdate workspace
# Usage: Run before large edits to save current source files.
# Creates backups\<yyyyMMdd_HHmmss> and copies listed files. Keeps 5 newest backups.

param(
    [string[]]$FilesToBackup = @("src\main.cpp"),
    [string]$FilesToBackupString = "",
    [int]$Keep = 5
)

# If a semicolon-separated string of files is supplied, split it into the array
if ($FilesToBackupString -and $FilesToBackupString.Trim() -ne "") {
    $arr = $FilesToBackupString -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }
    if ($arr.Count -gt 0) { $FilesToBackup = $arr }
}

# base workspace root (one directory up from scripts folder)
$workspaceRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
$dest = Join-Path -Path $workspaceRoot -ChildPath (Join-Path "backups" $ts)
if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest -Force | Out-Null }

foreach ($f in $FilesToBackup) {
    # normalize relative paths
    $src = Join-Path $workspaceRoot $f
    if (Test-Path $src) {
        $relDir = Split-Path -Path $f -Parent
        $targetDir = if ($relDir -and $relDir -ne '.') { Join-Path $dest $relDir } else { $dest }
        if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Path $targetDir -Force | Out-Null }
        Copy-Item -Path $src -Destination (Join-Path $dest (Split-Path $f -Leaf)) -Force
    } else {
        Write-Warning "File not found: $f"
    }
}

Set-Content -Path (Join-Path $dest 'meta.txt') -Value ("backup created: $ts`nfiles: $($FilesToBackup -join ', ')") -Encoding UTF8

# ensure backups root exists and prune older backups, keep $Keep newest
$backupsRoot = Join-Path $workspaceRoot "backups"
if (Test-Path $backupsRoot) {
    Get-ChildItem -Path $backupsRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -Skip $Keep | ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
}

Write-Host "Backup created at: $dest"