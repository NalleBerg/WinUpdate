# Category Correlation Analysis Script
# Analyzes tag co-occurrence patterns and infers missing categories
# Minimum 6 samples, 66.67% correlation threshold

param(
    [switch]$DryRun,  # Show what would be added without writing to DB
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$sqlite = Join-Path $scriptDir "sqlite3\sqlite3.exe"
$logPath = Join-Path $scriptDir "correlation_analysis.log"

$CORRELATION_THRESHOLD = 0.6667  # 66.67%
$MIN_SAMPLE_SIZE = 6

# Log function
function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] [$Level] $Message"
    
    $color = switch ($Level) {
        "ERROR" { "Red" }
        "WARNING" { "Yellow" }
        "SUCCESS" { "Green" }
        "VERBOSE" { "Gray" }
        default { "White" }
    }
    
    Write-Host $logMessage -ForegroundColor $color
    Add-Content -Path $logPath -Value $logMessage
}

Write-Log "=== Correlation Analysis Started ===" "SUCCESS"
Write-Log "Database: $dbPath"
Write-Log "Correlation threshold: $($CORRELATION_THRESHOLD * 100)%"
Write-Log "Minimum sample size: $MIN_SAMPLE_SIZE"
if ($DryRun) { Write-Log "DRY RUN MODE - No changes will be written" "WARNING" }

# Get ignored tags
Write-Log "Loading ignored tags..."
$ignoredTags = @()
try {
    $result = & $sqlite $ignoreDbPath "SELECT tag FROM ignored_tags;" 2>&1
    if ($LASTEXITCODE -eq 0) {
        $ignoredTags = $result | Where-Object { $_ -ne "" }
        Write-Log "Loaded $($ignoredTags.Count) ignored tags"
    }
}
catch {
    Write-Log "Error loading ignored tags: $_" "ERROR"
}

# Get all tags from all packages
Write-Log "Analyzing tag patterns across all packages..."
$tagsByPackage = @{}
$allFunctionalTags = @{}

$query = @"
SELECT a.id, a.package_id, GROUP_CONCAT(c.category_name, '|') as tags
FROM apps a
LEFT JOIN app_categories ac ON a.id = ac.app_id
LEFT JOIN categories c ON ac.category_id = c.id
GROUP BY a.id;
"@

try {
    $results = & $sqlite $dbPath $query 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Log "Error querying database: $results" "ERROR"
        exit 1
    }
    
    foreach ($line in $results) {
        if ($line -match '^(\d+)\|([^|]+)\|(.*)$') {
            $appId = $matches[1]
            $packageId = $matches[2]
            $tagsStr = $matches[3]
            
            if ($tagsStr -and $tagsStr -ne "") {
                $tags = $tagsStr -split '\|' | Where-Object { 
                    $tag = $_.Trim().ToLower()
                    $tag -ne "" -and $ignoredTags -notcontains $tag
                }
                
                if ($tags.Count -gt 0) {
                    $tagsByPackage[$packageId] = @{
                        Id = $appId
                        Tags = $tags
                    }
                    
                    foreach ($tag in $tags) {
                        $tagLower = $tag.ToLower()
                        if (-not $allFunctionalTags.ContainsKey($tagLower)) {
                            $allFunctionalTags[$tagLower] = 0
                        }
                        $allFunctionalTags[$tagLower]++
                    }
                }
            }
        }
    }
    
    Write-Log "Analyzed $($tagsByPackage.Count) packages with functional tags"
    Write-Log "Found $($allFunctionalTags.Count) unique functional tags"
}
catch {
    Write-Log "Error analyzing tags: $_" "ERROR"
    exit 1
}

# Build co-occurrence matrix
Write-Log "Building tag co-occurrence matrix..."
$coOccurrence = @{}

foreach ($pkg in $tagsByPackage.GetEnumerator()) {
    $tags = $pkg.Value.Tags
    
    # For each tag, count how often it appears with other tags
    for ($i = 0; $i -lt $tags.Count; $i++) {
        $tag1 = $tags[$i].ToString().ToLower()
        
        for ($j = 0; $j -lt $tags.Count; $j++) {
            if ($i -eq $j) { continue }
            
            $tag2 = $tags[$j].ToString().ToLower()
            $key = "$tag1->$tag2"
            
            if (-not $coOccurrence.ContainsKey($key)) {
                $coOccurrence[$key] = 0
            }
            $coOccurrence[$key]++
        }
    }
}

Write-Log "Built co-occurrence matrix with $($coOccurrence.Count) relationships"

# Find strong correlations
Write-Log "Finding strong correlations (>= $($CORRELATION_THRESHOLD * 100)%, min $MIN_SAMPLE_SIZE samples)..."
$inferenceRules = @()

foreach ($relation in $coOccurrence.GetEnumerator()) {
    if ($relation.Key -match '^(.+)->(.+)$') {
        $sourceTag = $matches[1]
        $targetTag = $matches[2]
        $coCount = $relation.Value
        $sourceCount = $allFunctionalTags[$sourceTag]
        
        if ($sourceCount -ge $MIN_SAMPLE_SIZE) {
            $correlation = $coCount / $sourceCount
            
            if ($correlation -ge $CORRELATION_THRESHOLD) {
                $inferenceRules += @{
                    Source = $sourceTag
                    Target = $targetTag
                    Correlation = $correlation
                    Samples = $sourceCount
                    CoOccurrences = $coCount
                }
            }
        }
    }
}

Write-Log "Found $($inferenceRules.Count) strong correlations"

# Sort by correlation strength
$inferenceRules = $inferenceRules | Sort-Object -Property Correlation -Descending

# Display top correlations
Write-Log "`n=== TOP CORRELATION RULES ===" "SUCCESS"
$displayCount = [Math]::Min(20, $inferenceRules.Count)
for ($i = 0; $i -lt $displayCount; $i++) {
    $rule = $inferenceRules[$i]
    $pct = [math]::Round($rule.Correlation * 100, 1)
    Write-Log "  '$($rule.Source)' -> '$($rule.Target)' | ${pct}% ($($rule.CoOccurrences)/$($rule.Samples) samples)"
}

if ($inferenceRules.Count -gt $displayCount) {
    Write-Log "  ... and $($inferenceRules.Count - $displayCount) more rules"
}

# Apply inference rules
Write-Log "`n=== Applying Inference Rules ===" "SUCCESS"
$categoriesAdded = 0
$packagesUpdated = 0

foreach ($rule in $inferenceRules) {
    # Find packages that have source tag but not target tag
    $candidatePackages = @()
    
    foreach ($pkg in $tagsByPackage.GetEnumerator()) {
        $tagsLower = $pkg.Value.Tags | ForEach-Object { $_.ToLower() }
        
        if ($tagsLower -contains $rule.Source -and $tagsLower -notcontains $rule.Target) {
            $candidatePackages += @{
                Id = $pkg.Value.Id
                PackageId = $pkg.Name
                Tags = $pkg.Value.Tags
            }
        }
    }
    
    if ($candidatePackages.Count -gt 0) {
        $pct = [math]::Round($rule.Correlation * 100, 1)
        Write-Log "Rule: '$($rule.Source)' -> '$($rule.Target)' (${pct}%)" "VERBOSE"
        Write-Log "  Found $($candidatePackages.Count) packages to update" "VERBOSE"
        
        foreach ($candidate in $candidatePackages) {
            if ($Verbose) {
                Write-Log "    Adding '$($rule.Target)' to $($candidate.PackageId)" "VERBOSE"
            }
            
            if (-not $DryRun) {
                # Get or create category ID
                $getCategoryQuery = "SELECT id FROM categories WHERE LOWER(category_name) = LOWER('$($rule.Target)');"
                $categoryId = & $sqlite $dbPath $getCategoryQuery 2>&1 | Select-Object -First 1
                
                if (-not $categoryId -or $categoryId -eq "") {
                    # Create new category
                    $insertCategoryQuery = "INSERT INTO categories (category_name) VALUES ('$($rule.Target)');"
                    & $sqlite $dbPath $insertCategoryQuery 2>&1 | Out-Null
                    $categoryId = & $sqlite $dbPath "SELECT last_insert_rowid();" 2>&1 | Select-Object -First 1
                }
                
                # Add to app_categories if not exists
                $checkQuery = "SELECT COUNT(*) FROM app_categories WHERE app_id = $($candidate.Id) AND category_id = $categoryId;"
                $exists = & $sqlite $dbPath $checkQuery 2>&1 | Select-Object -First 1
                
                if ($exists -eq "0") {
                    $insertQuery = "INSERT INTO app_categories (app_id, category_id) VALUES ($($candidate.Id), $categoryId);"
                    & $sqlite $dbPath $insertQuery 2>&1 | Out-Null
                    
                    if ($LASTEXITCODE -eq 0) {
                        $categoriesAdded++
                    }
                }
            }
            else {
                $categoriesAdded++
            }
        }
        
        $packagesUpdated += $candidatePackages.Count
    }
}

Write-Log "`n=== RESULTS ===" "SUCCESS"
if ($DryRun) {
    Write-Log "DRY RUN: Would add $categoriesAdded categories to $packagesUpdated packages" "WARNING"
}
else {
    Write-Log "Added $categoriesAdded categories to $packagesUpdated package entries" "SUCCESS"
}

# Generate statistics
Write-Log "`n=== Final Statistics ===" "SUCCESS"
$statsQuery = @"
SELECT 
    COUNT(DISTINCT a.id) as total_packages,
    COUNT(DISTINCT CASE WHEN ac.app_id IS NOT NULL THEN a.id END) as packages_with_categories,
    COUNT(*) as total_category_assignments,
    COUNT(DISTINCT c.id) as unique_categories
FROM apps a
LEFT JOIN app_categories ac ON a.id = ac.app_id
LEFT JOIN categories c ON ac.category_id = c.id;
"@

$stats = & $sqlite $dbPath $statsQuery 2>&1 | Select-Object -First 1
if ($stats -match '^(\d+)\|(\d+)\|(\d+)\|(\d+)$') {
    $totalPackages = [int]$matches[1]
    $packagesWithCategories = [int]$matches[2]
    $totalAssignments = [int]$matches[3]
    $uniqueCategories = [int]$matches[4]
    
    $coveragePct = [math]::Round(($packagesWithCategories / $totalPackages) * 100, 1)
    $avgCategoriesPerPackage = [math]::Round($totalAssignments / $totalPackages, 2)
    
    Write-Log "Total packages: $totalPackages"
    Write-Log "Packages with categories: $packagesWithCategories (${coveragePct}%)"
    Write-Log "Total category assignments: $totalAssignments"
    Write-Log "Unique categories: $uniqueCategories"
    Write-Log "Average categories per package: $avgCategoriesPerPackage"
}

Write-Log "`n=== Correlation Analysis Complete ===" "SUCCESS"
