# Add Missing Installed Packages Script
# Called by WinProgramUpdater during Step 2.5
# Adds packages that are in installed_apps but missing from main apps table

param(
    [Parameter(Mandatory=$true)]
    [string]$DatabasePath
)

$ErrorActionPreference = "Stop"

# Find sqlite3.exe - it's in the same directory as the executables
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exeDir = Split-Path -Parent $scriptDir
$sqliteExe = Join-Path $exeDir "sqlite3\sqlite3.exe"

# Fallback: check if sqlite3 is in build source
if (-not (Test-Path $sqliteExe)) {
    $buildSourceDir = Split-Path -Parent (Split-Path -Parent $exeDir)
    $sqliteExe = Join-Path $buildSourceDir "sqlite3\sqlite3.exe"
}

if (-not (Test-Path $sqliteExe)) {
    Write-Host "  ERROR: sqlite3.exe not found" -ForegroundColor Red
    exit 1
}

Write-Host "  Checking for missing installed packages..." -ForegroundColor Yellow
Write-Host "  Database: $DatabasePath" -ForegroundColor Gray
Write-Host "  SQLite: $sqliteExe" -ForegroundColor Gray

$query = "SELECT i.package_id FROM installed_apps i LEFT JOIN apps a ON i.package_id = a.package_id WHERE a.package_id IS NULL;"
$missingPackages = & $sqliteExe $DatabasePath $query

Write-Host "  Query returned: [$missingPackages]" -ForegroundColor Gray

if ([string]::IsNullOrWhiteSpace($missingPackages)) {
    Write-Host "  No missing packages found." -ForegroundColor Green
    exit 0
}

$packageList = $missingPackages -split "`n" | Where-Object { $_.Trim().Length -gt 0 }
Write-Host "  Found $($packageList.Count) missing package(s)" -ForegroundColor Cyan
Write-Host "  Packages: $($packageList -join ', ')" -ForegroundColor Gray

$addedCount = 0
$failedCount = 0

foreach ($packageId in $packageList) {
    $packageId = $packageId.Trim()
    if ([string]::IsNullOrWhiteSpace($packageId)) { continue }
    
    Write-Host "    Adding: $packageId" -ForegroundColor White
    
    try {
        # Run winget show
        $output = winget show "$packageId" --accept-source-agreements 2>&1 | Out-String
        
        # Parse output
        $lines = $output -split "`n"
        $name = ""
        $version = ""
        $publisher = ""
        $moniker = ""
        $description = ""
        $homepage = ""
        $license = ""
        $tags = @()
        $inTags = $false
        
        foreach ($line in $lines) {
            $line = $line.Trim()
            if ($line.Length -eq 0) { continue }
            
            # First line: "Found <Name> [PackageId]"
            if ($line -match "^Found (.+?) \[.+?\]") {
                $name = $matches[1]
                continue
            }
            
            # Parse field: value lines
            if ($line -match "^(.+?):\s*(.*)$") {
                $field = $matches[1].Trim()
                $value = $matches[2].Trim()
                
                switch ($field) {
                    "Version" { $version = $value }
                    "Publisher" { $publisher = $value }
                    "Moniker" { $moniker = $value }
                    "Description" { $description = $value }
                    "Homepage" { $homepage = $value }
                    "License" { $license = $value }
                    "Tags" { $inTags = $true }
                    "Installer" { $inTags = $false }
                }
            }
            # Tags are indented lines
            elseif ($inTags -and $line.Length -gt 0 -and $line -notmatch ":") {
                $tag = $line.Trim()
                if ($tag.Length -gt 0) {
                    $tags += $tag
                }
            }
        }
        
        if ([string]::IsNullOrWhiteSpace($name)) {
            Write-Host "      Failed to parse package info" -ForegroundColor Red
            $failedCount++
            continue
        }
        
        # Escape single quotes for SQL
        $name = $name -replace "'", "''"
        $version = $version -replace "'", "''"
        $publisher = $publisher -replace "'", "''"
        $moniker = $moniker -replace "'", "''"
        $description = $description -replace "'", "''"
        $homepage = $homepage -replace "'", "''"
        $license = $license -replace "'", "''"
        $packageId = $packageId -replace "'", "''"
        
        # Insert into apps table
        $insertSql = @"
INSERT OR IGNORE INTO apps (package_id, name, version, publisher, moniker, description, homepage, license)
VALUES ('$packageId', '$name', '$version', '$publisher', '$moniker', '$description', '$homepage', '$license');
"@
        
        & $sqliteExe $DatabasePath $insertSql
        
        # Insert tags
        foreach ($tag in $tags) {
            $tag = $tag -replace "'", "''"
            $tagSql = "INSERT OR IGNORE INTO app_tags (package_id, tag) VALUES ('$packageId', '$tag');"
            & $sqliteExe $DatabasePath $tagSql
        }
        
        Write-Host "      Added: $name (v$version) with $($tags.Count) tags" -ForegroundColor Green
        $addedCount++
        
    } catch {
        Write-Host "      Error: $_" -ForegroundColor Red
        $failedCount++
    }
}

Write-Host ""
Write-Host "  Summary: $addedCount added, $failedCount failed" -ForegroundColor Cyan
Write-Host "  Script completed" -ForegroundColor Green
exit 0

Write-Host ""
Write-Host "  Summary: $addedCount added, $failedCount failed" -ForegroundColor Cyan
exit 0
