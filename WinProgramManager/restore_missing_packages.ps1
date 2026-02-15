# Compare current DB with backup and restore missing packages with proper UTF-8 encoding

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$currentDb = ".\WinProgramManager\WinProgramManager.db"
$backupDb = "..\DB\WinProgramManager_20260121_142323.db"
$sqlite = ".\sqlite3\sqlite3.exe"

Write-Host "Finding missing packages..." -ForegroundColor Cyan

# Get package IDs that are in backup but not in current
$missing = & $sqlite $backupDb "SELECT package_id FROM apps WHERE package_id NOT IN (SELECT package_id FROM apps WHERE 1 ATTACH DATABASE '$currentDb' AS current);" 2>&1

# Better approach - use two separate queries
$backupIds = & $sqlite $backupDb "SELECT package_id FROM apps ORDER BY package_id;" | Sort-Object
$currentIds = & $sqlite $currentDb "SELECT package_id FROM apps ORDER BY package_id;" | Sort-Object

$missingPackages = Compare-Object -ReferenceObject $backupIds -DifferenceObject $currentIds | Where-Object { $_.SideIndicator -eq '<=' } | Select-Object -ExpandProperty InputObject

$total = $missingPackages.Count
Write-Host "Found $total missing packages`n" -ForegroundColor Yellow

$added = 0
$failed = 0
$current = 0

foreach ($packageId in $missingPackages) {
    $current++
    
    # Skip numeric-only or invalid package IDs
    if ($packageId -match '^\d+$' -or $packageId -match '^[\d.]+[a-z]\d*$') {
        Write-Host "[$current/$total] $packageId - Skipped (invalid ID)" -ForegroundColor DarkGray
        $failed++
        continue
    }
    
    Write-Host "[$current/$total] $packageId" -ForegroundColor Cyan
    Write-Host "  Querying winget..." -NoNewline
    
    try {
        # Query winget with timeout using Start-Job (15 second timeout)
        $job = Start-Job -ScriptBlock {
            param($pkg)
            winget show $pkg --accept-source-agreements 2>$null | Where-Object { $_.Trim() -notmatch '^[-\\/|]$' }
        } -ArgumentList $packageId
        
        $completed = Wait-Job -Job $job -Timeout 15
        
        if ($completed) {
            $info = Receive-Job -Job $job -ErrorAction SilentlyContinue
            Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
        } else {
            Stop-Job -Job $job -ErrorAction SilentlyContinue
            Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
            Write-Host " ✗ Timeout" -ForegroundColor Yellow
            $failed++
            continue
        }
        
        if ($info) {
            # Parse name from "Found <Name> [PackageId]" line
            $firstLine = ($info | Where-Object { $_ -match '^Found' } | Select-Object -First 1).Trim()
            
            if ($firstLine -match '^Found\s+(.+?)\s+\[') {
                $name = $matches[1].Trim()
                
                # Use temp file for proper UTF-8 handling
                $tempFile = [System.IO.Path]::GetTempFileName()
                $name | Out-File -FilePath $tempFile -Encoding UTF8 -NoNewline
                $escapedName = (Get-Content $tempFile -Raw -Encoding UTF8) -replace "'", "''"
                Remove-Item $tempFile -Force
                
                # Insert into database
                $insertSql = "INSERT OR IGNORE INTO apps (package_id, name) VALUES ('$packageId', '$escapedName');"
                & $sqlite $currentDb $insertSql
                
                Write-Host " ✓ Added: $name" -ForegroundColor Green
                $added++
            } else {
                Write-Host " ✗ No name found" -ForegroundColor Red
                $failed++
            }
        } else {
            Write-Host " ✗ Not found" -ForegroundColor Red
            $failed++
        }
    } catch {
        Write-Host " ✗ Error" -ForegroundColor Red
        $failed++
    } finally {
        # Ensure any remaining jobs are cleaned up
        Get-Job | Where-Object { $_.Name -like 'Job*' -and $_.State -ne 'Running' } | Remove-Job -Force -ErrorAction SilentlyContinue
    }
    
    Start-Sleep -Milliseconds 50
}

Write-Host "`n" + ("="*60) -ForegroundColor Cyan
Write-Host "Summary:" -ForegroundColor Cyan
Write-Host "  Added: $added" -ForegroundColor Green
Write-Host "  Failed: $failed" -ForegroundColor Red
Write-Host "  Total: $total" -ForegroundColor White
Write-Host "`nMissing packages restored successfully!" -ForegroundColor Green
