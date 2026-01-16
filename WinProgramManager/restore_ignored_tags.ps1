# Restore All Missing Tags to Database
# Adds all tags from winget that are missing in the database

param(
    [int]$MaxPackages = 0,
    [int]$StartFrom = 1
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$sqlite = Join-Path $scriptDir "sqlite3\sqlite3.exe"
$logPath = Join-Path $scriptDir "restore_tags.log"

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] [$Level] $Message"
    
    $color = switch ($Level) {
        "ERROR" { "Red" }
        "WARNING" { "Yellow" }
        "SUCCESS" { "Green" }
        "ADDED" { "Cyan" }
        default { "White" }
    }
    
    Write-Host $logMessage -ForegroundColor $color
    Add-Content -Path $logPath -Value $logMessage -Encoding UTF8
}

$scriptStartTime = Get-Date
Write-Log "========================================" "SUCCESS"
Write-Log "=== Restore All Missing Tags Started ===" "SUCCESS"
Write-Log "Start Time: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))" "SUCCESS"
Write-Log "========================================" "SUCCESS"

# Get all packages
Write-Log "Fetching packages from database..." "INFO"
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

# Apply StartFrom and MaxPackages filters
if ($StartFrom -gt 1) {
    $packageList = $packageList[($StartFrom-1)..($packageList.Count-1)]
    Write-Log "Starting from package #$StartFrom" "WARNING"
}

if ($MaxPackages -gt 0 -and $MaxPackages -lt $packageList.Count) {
    $packageList = $packageList[0..($MaxPackages-1)]
    $processCount = $MaxPackages
    Write-Log "Limited to $processCount packages (from #$StartFrom to #$($StartFrom + $processCount - 1))" "WARNING"
}
else {
    $processCount = $packageList.Count
}

Write-Log "Processing $processCount packages..." "SUCCESS"
$startTime = Get-Date
$processed = 0
$skipped = 0
$tagsAdded = 0
$packagesUpdated = 0
$errors = 0

foreach ($pkg in $packageList) {
    $processed++
    $pkgStartTime = Get-Date
    
    # Calculate actual package number
    $actualPkgNum = $StartFrom + $processed - 1
    
    # Progress display with elapsed time
    $elapsed = (Get-Date) - $startTime
    $elapsedHours = [math]::Floor($elapsed.TotalHours)
    $elapsedMinutes = [math]::Floor($elapsed.TotalMinutes % 60)
    $elapsedSeconds = [math]::Floor($elapsed.TotalSeconds % 60)
    $elapsedStr = if ($elapsedHours -gt 0) {
        "${elapsedHours}h ${elapsedMinutes}m"
    } elseif ($elapsedMinutes -gt 0) {
        "${elapsedMinutes}m ${elapsedSeconds}s"
    } else {
        "${elapsedSeconds}s"
    }
    
    $rate = if ($elapsed.TotalSeconds -gt 0) { $processed / $elapsed.TotalSeconds } else { 0 }
    $remaining = if ($rate -gt 0) { ($processCount - $processed) / $rate } else { 0 }
    $etaMin = [math]::Round($remaining / 60, 1)
    $etaHours = [math]::Round($remaining / 3600, 1)
    
    if ($etaHours -gt 1) {
        Write-Log "[$actualPkgNum/$total] $($pkg.PackageId) | Elapsed: $elapsedStr | Updated: $packagesUpdated | ETA: ${etaHours}h" "INFO"
    }
    else {
        Write-Log "[$actualPkgNum/$total] $($pkg.PackageId) | Elapsed: $elapsedStr | Updated: $packagesUpdated | ETA: ${etaMin}m" "INFO"
    }
    
    # Initialize per-package counter
    $addedThisPackage = 0
    
    # Get DB tag count
    Write-Log "  Checking DB tag count..." "INFO"
    $dbTagCountQuery = "SELECT COUNT(*) FROM app_categories ac WHERE ac.app_id = $($pkg.Id);"
    $dbTagCount = & $sqlite $dbPath $dbTagCountQuery 2>&1 | Select-Object -First 1
    if (-not $dbTagCount) { $dbTagCount = 0 }
    $dbTagCount = [int]$dbTagCount
    Write-Log "  DB has $dbTagCount tags" "INFO"
    
    # Query winget and count tags
    Write-Log "  Querying winget..." "INFO"
    
    $output = winget show $pkg.PackageId --accept-source-agreements 2>$null
    
    if ($LASTEXITCODE -ne 0) {
        Write-Log "  ERROR: Failed to query winget" "ERROR"
        $errors++
        continue
    }
    
    # Count tags
    $inTags = $false
    $wingetTagCount = 0
    $allTags = @()
    
    foreach ($line in $output) {
        if ($line -match '^\s*Tags:\s*$') {
            $inTags = $true
            continue
        }
        
        if ($inTags) {
            if ($line -match '^\s*$' -or $line -match '^\S') {
                break
            }
            
            $tag = $line.Trim().ToLower()
            if ($tag -ne "") {
                $wingetTagCount++
                $allTags += $tag
            }
        }
    }
    
    Write-Log "  Winget has $wingetTagCount tags" "INFO"
    
    # Skip if DB has enough tags
    if ($dbTagCount -ge $wingetTagCount) {
        $skipped++
        $pkgElapsed = (Get-Date) - $pkgStartTime
        Write-Log "  ⊘ Complete ($([math]::Round($pkgElapsed.TotalSeconds, 1))s)" "INFO"
        continue
    }
    
    # Get existing DB tags for this package (case-sensitive for Unicode support)
    Write-Log "  Getting existing DB tags..." "INFO"
    $dbTagsQuery = "SELECT c.category_name FROM app_categories ac JOIN categories c ON ac.category_id = c.id WHERE ac.app_id = $($pkg.Id);"
    $dbTagsResult = & $sqlite $dbPath $dbTagsQuery 2>&1
    $dbTags = @($dbTagsResult | Where-Object { $_ -ne "" } | ForEach-Object { $_.ToLower() })
    
    # Find missing tags
    $missingTags = @()
    foreach ($tag in $allTags) {
        if ($dbTags -notcontains $tag) {
            $missingTags += $tag
        }
    }
    
    if ($missingTags.Count -gt 0) {
        Write-Log "  Adding $($missingTags.Count) missing tags..." "INFO"
        
        foreach ($tag in $missingTags) {
            
            # Get or create category (case-insensitive for better matching)
            $getCatQuery = "SELECT id FROM categories WHERE category_name = '$($tag.Replace("'", "''"))' COLLATE NOCASE;"
            $categoryId = & $sqlite $dbPath $getCatQuery 2>&1 | Select-Object -First 1
            
            if (-not $categoryId -or $categoryId -eq "" -or $categoryId -match "Error") {
                $insertCatQuery = "INSERT OR IGNORE INTO categories (category_name) VALUES ('$($tag.Replace("'", "''"))');"
                $catInsertResult = & $sqlite $dbPath $insertCatQuery 2>&1
                if ($LASTEXITCODE -ne 0 -and -not ($catInsertResult -match "Error")) {
                    Write-Log "    ✗ $tag (failed to create category)" "ERROR"
                    continue
                }
                # Re-query to get the ID (in case INSERT OR IGNORE skipped due to existing)
                $categoryId = & $sqlite $dbPath $getCatQuery 2>&1 | Select-Object -First 1
            }
            
            # Verify we have a valid category ID
            if (-not $categoryId -or $categoryId -eq "" -or $categoryId -match "Error") {
                Write-Log "    ✗ $tag (invalid category ID)" "ERROR"
                continue
            }
            
            # Check if already exists for this app BEFORE trying to insert
            $checkExistQuery = "SELECT COUNT(*) FROM app_categories WHERE app_id = $($pkg.Id) AND category_id = $categoryId;"
            $alreadyExists = & $sqlite $dbPath $checkExistQuery 2>&1 | Select-Object -First 1
            
            if ($alreadyExists -eq "1") {
                Write-Log "    ⊘ $tag (already in DB)" "INFO"
                continue
            }
            
            # Add to app_categories
            $insertQuery = "INSERT INTO app_categories (app_id, category_id) VALUES ($($pkg.Id), $categoryId);"
            $insertResult = & $sqlite $dbPath $insertQuery 2>&1
            
            if ($LASTEXITCODE -eq 0 -and -not ($insertResult -match "Error")) {
                # Verify the tag was added
                $verifyQuery = "SELECT COUNT(*) FROM app_categories WHERE app_id = $($pkg.Id) AND category_id = $categoryId;"
                $verified = & $sqlite $dbPath $verifyQuery 2>&1 | Select-Object -First 1
                if ($verified -eq "1") {
                    Write-Log "    ✓ $tag" "ADDED"
                    $tagsAdded++
                    $addedThisPackage++
                }
                else {
                    Write-Log "    ✗ $tag (verification failed)" "ERROR"
                }
            }
            else {
                Write-Log "    ✗ $tag (insert failed: $insertResult)" "ERROR"
            }
        }
        
        if ($addedThisPackage -gt 0) {
            $packagesUpdated++
            $pkgElapsed = (Get-Date) - $pkgStartTime
            Write-Log "  ✓ Added $addedThisPackage tags in $([math]::Round($pkgElapsed.TotalSeconds, 1))s" "SUCCESS"
        }
        else {
            $pkgElapsed = (Get-Date) - $pkgStartTime
            Write-Log "  ⊘ No tags added ($([math]::Round($pkgElapsed.TotalSeconds, 1))s)" "INFO"
        }
    } # End of if ($missingTags.Count -gt 0)
    
    # Milestone reports
    if ($processed % 1000 -eq 0) {
        $elapsed = (Get-Date) - $startTime
        $rate = $processed / $elapsed.TotalSeconds
        
        Write-Log "`n=== MILESTONE: $processed packages ===" "SUCCESS"
        Write-Log "  Skipped: $skipped" "INFO"
        Write-Log "  Tags added: $tagsAdded" "INFO"
        Write-Log "  Packages updated: $packagesUpdated" "INFO"
        Write-Log "  Errors: $errors" "INFO"
        Write-Log "  Rate: $([math]::Round($rate, 2)) pkg/sec`n" "INFO"
    }
} # End of foreach loop

$totalElapsed = (Get-Date) - $startTime
$scriptEndTime = Get-Date

Write-Log "`n========================================" "SUCCESS"
Write-Log "=== COMPLETE ===" "SUCCESS"
Write-Log "Start Time: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))" "SUCCESS"
Write-Log "End Time:   $($scriptEndTime.ToString('yyyy-MM-dd HH:mm:ss'))" "SUCCESS"
Write-Log "========================================" "SUCCESS"
Write-Log "Processed: $processed packages" "INFO"
Write-Log "Skipped: $skipped" "INFO"
Write-Log "Tags added: $tagsAdded" "SUCCESS"
Write-Log "Packages updated: $packagesUpdated" "SUCCESS"
Write-Log "Errors: $errors" "WARNING"
Write-Log "Total time: $([math]::Round($totalElapsed.TotalHours, 2)) hours" "INFO"

# Final statistics
Write-Log "`n=== Final Database Statistics ===" "SUCCESS"
$statsQuery = "SELECT COUNT(DISTINCT a.id) as total_packages, COUNT(DISTINCT CASE WHEN ac.app_id IS NOT NULL THEN a.id END) as packages_with_categories, COUNT(*) as total_category_assignments, COUNT(DISTINCT c.id) as unique_categories FROM apps a LEFT JOIN app_categories ac ON a.id = ac.app_id LEFT JOIN categories c ON ac.category_id = c.id;"

$stats = & $sqlite $dbPath $statsQuery 2>&1 | Select-Object -First 1
if ($stats -match '^(\d+)\|(\d+)\|(\d+)\|(\d+)$') {
    $totalPackages = [int]$matches[1]
    $packagesWithCategories = [int]$matches[2]
    $totalAssignments = [int]$matches[3]
    $uniqueCategories = [int]$matches[4]
    
    $coveragePct = [math]::Round(($packagesWithCategories / $totalPackages) * 100, 1)
    $avgCategoriesPerPackage = [math]::Round($totalAssignments / $totalPackages, 2)
    
    Write-Log "Total packages: $totalPackages" "INFO"
    Write-Log "Packages with categories: $packagesWithCategories (${coveragePct}%)" "INFO"
    Write-Log "Total category assignments: $totalAssignments" "INFO"
    Write-Log "Unique categories: $uniqueCategories" "INFO"
    Write-Log "Average categories per package: $avgCategoriesPerPackage" "INFO"
}

Write-Log "`nNext step: Run correlation analysis" "SUCCESS"
