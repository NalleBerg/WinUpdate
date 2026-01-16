# Smart Category Inference
# Infers missing categories from package names, descriptions, and publisher patterns

param(
    [switch]$DryRun,
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$sqlite = Join-Path $scriptDir "sqlite3\sqlite3.exe"

Write-Host "=== Smart Category Inference ===" -ForegroundColor Cyan
if ($DryRun) { Write-Host "DRY RUN MODE" -ForegroundColor Yellow }

# Inference rules based on patterns
$inferenceRules = @(
    @{ Pattern = 'chrome|chromium'; Category = 'browser'; Field = 'name' }
    @{ Pattern = 'firefox|mozilla'; Category = 'browser'; Field = 'name' }
    @{ Pattern = 'edge|msedge'; Category = 'browser'; Field = 'name' }
    @{ Pattern = 'opera|vivaldi|brave'; Category = 'browser'; Field = 'name' }
    @{ Pattern = 'browser|nettleser'; Category = 'browser'; Field = 'description' }
    
    @{ Pattern = 'vscode|visual studio code'; Category = 'development'; Field = 'name' }
    @{ Pattern = 'notepad\+\+|sublime|atom'; Category = 'development'; Field = 'name' }
    @{ Pattern = 'git|github'; Category = 'development'; Field = 'name' }
    
    @{ Pattern = 'discord|slack|teams'; Category = 'communication'; Field = 'name' }
    @{ Pattern = 'zoom|skype|telegram'; Category = 'communication'; Field = 'name' }
    
    @{ Pattern = 'vlc|media player'; Category = 'media'; Field = 'name' }
    @{ Pattern = 'spotify|itunes|musicbee'; Category = 'media'; Field = 'name' }
    
    @{ Pattern = 'photoshop|gimp|paint\.net'; Category = 'graphics'; Field = 'name' }
    @{ Pattern = 'blender|inkscape'; Category = 'graphics'; Field = 'name' }
    
    @{ Pattern = '7-zip|winrar|winzip'; Category = 'utilities'; Field = 'name' }
    @{ Pattern = 'ccleaner|bleachbit'; Category = 'utilities'; Field = 'name' }
)

$categoriesAdded = 0
$packagesUpdated = 0

foreach ($rule in $inferenceRules) {
    Write-Host "`nRule: $($rule.Pattern) -> $($rule.Category)" -ForegroundColor Cyan
    
    # Convert regex pattern to SQL LIKE patterns
    $patterns = $rule.Pattern -split '\|'
    $likeConditions = @()
    foreach ($p in $patterns) {
        $p = $p.Replace('\+', '+').Replace('\.', '.')
        $likeConditions += "LOWER(a.$($rule.Field)) LIKE '%$p%'"
    }
    $whereClause = $likeConditions -join ' OR '
    
    # Find packages matching pattern but missing category
    $query = @"
SELECT a.id, a.package_id, a.name, a.description
FROM apps a
WHERE ($whereClause)
AND a.id NOT IN (
    SELECT ac.app_id FROM app_categories ac
    JOIN categories c ON ac.category_id = c.id
    WHERE LOWER(c.category_name) = '$($rule.Category)'
);
"@
    
    try {
        $results = & $sqlite $dbPath $query 2>&1
        $matches = @()
        
        foreach ($line in $results) {
            if ($line -match '^(\d+)\|([^|]+)\|([^|]*)\|(.*)$') {
                $matches += @{
                    Id = $matches[1]
                    PackageId = $matches[2]
                    Name = $matches[3]
                    Description = $matches[4]
                }
            }
        }
        
        if ($matches.Count -gt 0) {
            Write-Host "  Found $($matches.Count) packages to update" -ForegroundColor Green
            
            foreach ($match in $matches) {
                if ($Verbose) {
                    Write-Host "    Adding '$($rule.Category)' to $($match.PackageId)" -ForegroundColor Gray
                }
                
                if (-not $DryRun) {
                    # Get or create category
                    $getCatQuery = "SELECT id FROM categories WHERE LOWER(category_name) = LOWER('$($rule.Category)');"
                    $categoryId = & $sqlite $dbPath $getCatQuery 2>&1 | Select-Object -First 1
                    
                    if (-not $categoryId -or $categoryId -eq "") {
                        $insertCatQuery = "INSERT INTO categories (category_name) VALUES ('$($rule.Category)');"
                        & $sqlite $dbPath $insertCatQuery 2>&1 | Out-Null
                        $categoryId = & $sqlite $dbPath "SELECT last_insert_rowid();" 2>&1 | Select-Object -First 1
                    }
                    
                    # Add category to package
                    $insertQuery = "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($($match.Id), $categoryId);"
                    & $sqlite $dbPath $insertQuery 2>&1 | Out-Null
                    
                    if ($LASTEXITCODE -eq 0) {
                        $categoriesAdded++
                    }
                }
                else {
                    $categoriesAdded++
                }
            }
            
            $packagesUpdated += $matches.Count
        }
        else {
            Write-Host "  No packages found" -ForegroundColor Gray
        }
    }
    catch {
        Write-Host "  Error: $_" -ForegroundColor Red
    }
}

Write-Host "`n=== Results ===" -ForegroundColor Green
if ($DryRun) {
    Write-Host "Would add $categoriesAdded category assignments to $packagesUpdated packages"
}
else {
    Write-Host "Added $categoriesAdded category assignments to $packagesUpdated packages"
}

# Show final stats
$statsQuery = @"
SELECT 
    COUNT(DISTINCT a.id) as total,
    COUNT(DISTINCT CASE WHEN ac.app_id IS NOT NULL THEN a.id END) as with_cats
FROM apps a
LEFT JOIN app_categories ac ON a.id = ac.app_id;
"@

$stats = & $sqlite $dbPath $statsQuery 2>&1 | Select-Object -First 1
if ($stats -match '^(\d+)\|(\d+)$') {
    $total = [int]$matches[1]
    $withCats = [int]$matches[2]
    $pct = [math]::Round(($withCats / $total) * 100, 1)
    Write-Host "`nFinal: $withCats/$total packages with categories (${pct}%)" -ForegroundColor Cyan
}
