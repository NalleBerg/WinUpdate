# Add raw unfiltered tags to database for correlation analysis
# This adds an 'all_tags' column with ALL tags including ignored ones

param(
    [int]$MaxPackages = 0
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$sqlite = Join-Path $scriptDir "sqlite3\sqlite3.exe"

Write-Host "=== Adding Raw Tags Column ===" -ForegroundColor Cyan

# Add all_tags column if it doesn't exist
Write-Host "Adding all_tags column to apps table..."
$alterQuery = "ALTER TABLE apps ADD COLUMN all_tags TEXT;"
& $sqlite $dbPath $alterQuery 2>&1 | Out-Null

Write-Host "Fetching packages from database..."
$packagesQuery = "SELECT id, package_id FROM apps ORDER BY id;"
$packages = & $sqlite $dbPath $packagesQuery 2>&1

$packageList = @()
foreach ($line in $packages) {
    if ($line -match '^(\d+)\|(.+)$') {
        $packageList += @{
            Id = $matches[1]
            PackageId = $matches[2]
        }
    }
}

$total = $packageList.Count
if ($MaxPackages -gt 0 -and $MaxPackages -lt $total) {
    $total = $MaxPackages
    $packageList = $packageList[0..($MaxPackages-1)]
}

Write-Host "Processing $total packages..." -ForegroundColor Green
$processed = 0
$updated = 0
$startTime = Get-Date

foreach ($pkg in $packageList) {
    $processed++
    
    if ($processed % 100 -eq 0) {
        $elapsed = (Get-Date) - $startTime
        $rate = $processed / $elapsed.TotalSeconds
        $remaining = ($total - $processed) / $rate
        $eta = [math]::Round($remaining / 60, 1)
        Write-Host "[$processed/$total] $($pkg.PackageId) | ETA: ${eta}m" -ForegroundColor Gray
    }
    
    # Get raw tags from winget
    try {
        $output = winget show $pkg.PackageId --accept-source-agreements 2>$null
        
        if ($LASTEXITCODE -eq 0) {
            $inTags = $false
            $tags = @()
            
            foreach ($line in $output) {
                if ($line -match '^\s*Tags:\s*$') {
                    $inTags = $true
                    continue
                }
                
                if ($inTags) {
                    if ($line -match '^\s*$' -or $line -match '^\S') {
                        break
                    }
                    
                    $tag = $line.Trim()
                    if ($tag -ne "") {
                        $tags += $tag
                    }
                }
            }
            
            if ($tags.Count -gt 0) {
                $tagsStr = ($tags -join '|').Replace("'", "''")
                $updateQuery = "UPDATE apps SET all_tags = '$tagsStr' WHERE id = $($pkg.Id);"
                & $sqlite $dbPath $updateQuery 2>&1 | Out-Null
                
                if ($LASTEXITCODE -eq 0) {
                    $updated++
                }
            }
        }
    }
    catch {
        Write-Host "Error processing $($pkg.PackageId): $_" -ForegroundColor Red
    }
}

$elapsed = (Get-Date) - $startTime
Write-Host "`n=== Complete ===" -ForegroundColor Green
Write-Host "Processed: $processed packages"
Write-Host "Updated: $updated packages with tags"
Write-Host "Time: $([math]::Round($elapsed.TotalMinutes, 1)) minutes"
