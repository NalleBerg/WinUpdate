# WinGet Program Manager Database Builder
# This script processes all winget packages and categorizes them by functional type

param(
    [int]$BatchSize = 100,
    [int]$MaxPackages = 0,
    [switch]$Resume
)

$ErrorActionPreference = "Continue"
# Set console encoding to UTF-8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

# Script start time
$scriptStartTime = Get-Date
Write-Host "=== Script Started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss')) ===" -ForegroundColor Cyan

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$logPath = Join-Path $scriptDir "winget_import.log"

# Correlation settings
$CORRELATION_THRESHOLD = 0.6667  # 66.67%
$MIN_SAMPLE_SIZE = 6

# Log function
function Write-Log {
    param([string]$Message)
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Write-Host $logMessage
    Add-Content -Path $logPath -Value $logMessage
}

# Initialize ignore database
function Initialize-IgnoreDatabase {
    Write-Log "Initializing ignore database..."
    
    $ignoredTags = @(
        'foss', 'open-source', 'free', 'opensource', 'open source',
        'commercial', 'proprietary', 'shareware', 'freeware',
        'cross-platform', 'portable', 'lightweight', 'standalone',
        'cli', 'gui', 'console', 'terminal', 'command-line',
        '32-bit', '64-bit', 'x86', 'x64', 'arm', 'arm64',
        'windows', 'linux', 'mac', 'macos', 'android', 'ios',
        'electron', 'qt', 'gtk', 'java', 'python', 'node',
        'web', 'webapp', 'desktop', 'mobile',
        'unicode', 'utf-8', 'i18n', 'l10n',
        'plugin', 'extension', 'addon', 'theme',
        'beta', 'alpha', 'stable', 'preview',
        'offline', 'online', 'cloud', 'local',
        # Browser engines and specific browser names
        'chromium', 'gecko', 'webkit', 'blink', 'trident',
        'ie', 'internet-explorer', 'edge', 'safari',
        'ungoogled-chromium', 'chromium-based',
        # VPN/Proxy protocols
        'shadowsocks', 'socks5', 'trojan', 'v2fly', 'v2ray',
        'vless', 'vmess', 'xray', 'xtls', 'wireguard',
        # Server/DevOps tools
        'docker', 'kubernetes', 'k8s', 'nginx', 'apache',
        'letsencrypt', 'ssl', 'tls',
        # Programming languages/frameworks
        'go', 'golang', 'rust', 'typescript', 'javascript', 'vue', 'react', 'angular',
        # AI/Service specific
        'chatgpt', 'chatgpt-app', 'copilot', 'deepseek', 'deepseek-r1',
        'mcp', 'llm', 'ai-assistant',
        # Deployment types
        'self-hosted', 'webui', 'web-ui', 'cron',
        # Too generic
        'application', 'app', 'apps', 'program', 'software', 'tool',
        'utility', 'webpage', 'website'
    )
    
    # Create ignore database
    $sql = @"
CREATE TABLE IF NOT EXISTS ignored_tags (
    tag_name TEXT PRIMARY KEY COLLATE NOCASE
);
"@
    
    & sqlite3\sqlite3.exe $ignoreDbPath $sql
    
    # Insert ignored tags
    foreach ($tag in $ignoredTags) {
        $insertSql = "INSERT OR IGNORE INTO ignored_tags (tag_name) VALUES ('$($tag.Replace("'", "''"))');"
        & sqlite3\sqlite3.exe $ignoreDbPath $insertSql
    }
    
    Write-Log "Ignore database initialized with $($ignoredTags.Count) tags"
}

# Initialize main database
function Initialize-MainDatabase {
    Write-Log "Initializing main database..."
    
    $sql = @"
CREATE TABLE IF NOT EXISTS apps (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id TEXT UNIQUE NOT NULL,
    name TEXT,
    version TEXT,
    publisher TEXT,
    description TEXT,
    processed_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    category_name TEXT UNIQUE NOT NULL COLLATE NOCASE
);

CREATE TABLE IF NOT EXISTS app_categories (
    app_id INTEGER,
    category_id INTEGER,
    PRIMARY KEY (app_id, category_id),
    FOREIGN KEY (app_id) REFERENCES apps(id),
    FOREIGN KEY (category_id) REFERENCES categories(id)
);

CREATE INDEX IF NOT EXISTS idx_package_id ON apps(package_id);
CREATE INDEX IF NOT EXISTS idx_category_name ON categories(category_name);
"@
    
    & sqlite3\sqlite3.exe $dbPath $sql
    Write-Log "Main database initialized"
}

# Check if tag is ignored
function Test-IgnoredTag {
    param([string]$Tag)
    
    $result = & sqlite3\sqlite3.exe $ignoreDbPath "SELECT COUNT(*) FROM ignored_tags WHERE tag_name = '$($Tag.Replace("'", "''"))' COLLATE NOCASE;"
    return [int]$result -gt 0
}

# Collect tags from package for analysis
function Get-PackageTags {
    param([string]$PackageId)
    
    try {
        $info = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        if ([string]::IsNullOrWhiteSpace($info)) {
            return @()
        }
        
        # Extract tags
        $tags = @()
        if ($info -match '(?s)Tags:\s*\n(.*?)(?:\n[A-Z][a-z]+:|$)') {
            $tagSection = $matches[1]
            $tags = ($tagSection -split '\n' | Where-Object {$_ -match '^\s+\S'} | ForEach-Object {$_.Trim()}) | Where-Object {$_}
        }
        
        # Filter to ASCII-only tags
        $validTags = @()
        foreach ($tag in $tags) {
            if ($tag -match '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
                $validTags += $tag.ToLower()
            }
        }
        
        return $validTags
    }
    catch {
        return @()
    }
}

# Build tag correlation map
function Build-CorrelationMap {
    param([array]$AllPackageTags)
    
    Write-Log "Building tag correlation map..."
    
    $tagCooccurrence = @{}
    
    # Count co-occurrences
    foreach ($packageTags in $AllPackageTags) {
        foreach ($tag1 in $packageTags) {
            if (-not $tagCooccurrence.ContainsKey($tag1)) {
                $tagCooccurrence[$tag1] = @{}
            }
            
            foreach ($tag2 in $packageTags) {
                if ($tag1 -ne $tag2) {
                    if (-not $tagCooccurrence[$tag1].ContainsKey($tag2)) {
                        $tagCooccurrence[$tag1][$tag2] = 0
                    }
                    $tagCooccurrence[$tag1][$tag2]++
                }
            }
        }
    }
    
    # Build inference rules
    $inferenceRules = @{}
    
    foreach ($tag in $tagCooccurrence.Keys) {
        $totalOccurrences = ($AllPackageTags | Where-Object { $_ -contains $tag }).Count
        
        if ($totalOccurrences -lt $MIN_SAMPLE_SIZE) {
            continue
        }
        
        foreach ($coTag in $tagCooccurrence[$tag].Keys) {
            # Skip if coTag is in ignore list
            if (Test-IgnoredTag $coTag) {
                continue
            }
            
            $coOccurrences = $tagCooccurrence[$tag][$coTag]
            $correlation = $coOccurrences / $totalOccurrences
            
            if ($correlation -ge $CORRELATION_THRESHOLD) {
                if (-not $inferenceRules.ContainsKey($tag)) {
                    $inferenceRules[$tag] = @()
                }
                $inferenceRules[$tag] += $coTag
                Write-Log "  Inference: '$tag' → '$coTag' (${totalOccurrences} samples, $([math]::Round($correlation * 100, 1))% correlation)"
            }
        }
    }
    
    return $inferenceRules
}

# Get or create category ID
function Get-CategoryId {
    param([string]$CategoryName)
    
    # Capitalize first letter of category name
    $CategoryName = $CategoryName.Substring(0,1).ToUpper() + $CategoryName.Substring(1)
    
    $existingSql = "SELECT id FROM categories WHERE category_name = '$($CategoryName.Replace("'", "''"))' COLLATE NOCASE;"
    $categoryId = & sqlite3\sqlite3.exe $dbPath $existingSql
    
    if ([string]::IsNullOrWhiteSpace($categoryId)) {
        $insertSql = "INSERT INTO categories (category_name) VALUES ('$($CategoryName.Replace("'", "''"))'); SELECT last_insert_rowid();"
        $categoryId = & sqlite3\sqlite3.exe $dbPath $insertSql
    }
    
    return $categoryId
}

# Process a single package
function Process-Package {
    param(
        [string]$PackageId,
        [hashtable]$InferenceRules
    )
    
    try {
        Write-Log "  [VERBOSE] Checking if $PackageId already exists in DB..."
        # Check if already processed
        $existsSql = "SELECT COUNT(*) FROM apps WHERE package_id = '$($PackageId.Replace("'", "''"))';"
        $exists = [int](& sqlite3\sqlite3.exe $dbPath $existsSql)
        
        if ($exists -gt 0) {
            Write-Log "  [SKIP] Already in database"
            return $true
        }
        
        Write-Log "  [VERBOSE] Calling winget show for $PackageId..."
        # Get package info
        $info = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        if ([string]::IsNullOrWhiteSpace($info)) {
            Write-Log "  [SKIP] No info for $PackageId"
            return $false
        }
        
        # Extract metadata
        $name = if ($info -match '(?m)^Found (.+?) \[') { $matches[1] } else { $PackageId }
        $version = if ($info -match '(?m)^Version: (.+)$') { $matches[1].Trim() } else { 'Unknown' }
        $publisher = if ($info -match '(?m)^Publisher: (.+)$') { $matches[1].Trim() } else { '' }
        $description = if ($info -match '(?m)^Description:\s*\n(.+?)(?=\n\n|\nHomepage:|\nLicense:|\nTags:)') { 
            $matches[1].Trim() -replace '\s+', ' ' | Select-Object -First 200 
        } else { '' }
        
        # Extract tags
        $tags = @()
        if ($info -match '(?s)Tags:\s*\n(.*?)(?:\n[A-Z][a-z]+:|$)') {
            $tagSection = $matches[1]
            $tags = ($tagSection -split '\n' | Where-Object {$_ -match '^\s+\S'} | ForEach-Object {$_.Trim()}) | Where-Object {$_}
        }
        
        # Filter functional categories only
        $functionalCategories = @()
        $tagLower = @()
        
        foreach ($tag in $tags) {
            # Skip ignored tags
            if (Test-IgnoredTag $tag) {
                continue
            }
            
            # Sanity check: Only keep tags with ASCII/Latin characters
            # Allow: letters, numbers, hyphens, underscores, dots
            if ($tag -match '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
                $tagLower += $tag.ToLower()
                $functionalCategories += $tag
            }
        }
        
        # Apply inference rules to add missing categories
        $inferredCategories = @()
        foreach ($tag in $tagLower) {
            if ($InferenceRules.ContainsKey($tag)) {
                foreach ($inferredTag in $InferenceRules[$tag]) {
                    if ($tagLower -notcontains $inferredTag -and $inferredCategories -notcontains $inferredTag) {
                        $inferredCategories += $inferredTag
                    }
                }
            }
        }
        
        # Add inferred categories
        $functionalCategories += $inferredCategories
        $functionalCategories = $functionalCategories | Select-Object -Unique
        
        # Insert app
        $insertAppSql = @"
INSERT INTO apps (package_id, name, version, publisher, description) 
VALUES ('$($PackageId.Replace("'", "''"))', '$($name.Replace("'", "''"))', '$($version.Replace("'", "''"))', '$($publisher.Replace("'", "''"))', '$($description.Replace("'", "''"))');
SELECT last_insert_rowid();
"@
        
        $appId = & sqlite3\sqlite3.exe $dbPath $insertAppSql
        
        # Insert categories
        foreach ($category in $functionalCategories) {
            $categoryId = Get-CategoryId $category
            $linkSql = "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($appId, $categoryId);"
            & sqlite3\sqlite3.exe $dbPath $linkSql | Out-Null
        }
        
        if ($functionalCategories.Count -gt 0) {
            Write-Log "  [OK] $PackageId -> Categories: $($functionalCategories -join ', ')"
        } else {
            Write-Log "  [OK] $PackageId -> No functional categories"
        }
        
        return $true
    }
    catch {
        Write-Log "  [ERROR] $PackageId : $_"
        return $false
    }
}

# Main execution
Write-Log "=== WinGet Database Builder Started ==="
Write-Log "Database: $dbPath"
Write-Log "Ignore DB: $ignoreDbPath"
Write-Log "Batch Size: $BatchSize"
if ($MaxPackages -gt 0) {
    Write-Log "Max Packages: $MaxPackages (TEST MODE)"
}

# Initialize databases
if (-not (Test-Path $ignoreDbPath)) {
    Initialize-IgnoreDatabase
}
if (-not (Test-Path $dbPath) -or -not $Resume) {
    Initialize-MainDatabase
}

# Get all winget packages
Write-Log "Fetching winget package list..."
$packages = @()

# Use winget search with dot (matches all package IDs since they all contain dots)
Write-Log "Running: winget search . --source winget --accept-source-agreements..."
$searchOutput = winget search . --source winget --accept-source-agreements 2>&1 | Out-String
$lines = $searchOutput -split "`n"

$inData = $false
foreach ($line in $lines) {
    # Find separator line (dashes)
    if ($line -match '^-{5,}') {
        $inData = $true
        continue
    }
    
    if (-not $inData -or [string]::IsNullOrWhiteSpace($line.Trim())) {
        continue
    }
    
    # Parse line: Name Id Version Source/Match
    # Split by multiple spaces (2 or more)
    $parts = $line -split '\s{2,}' | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
    
    if ($parts.Count -ge 2) {
        $id = $parts[1].Trim()
        # Skip obviously corrupted IDs (numbers followed by ASCII text without proper separator)
        if ($id -match '^\d+\.\d+[A-Za-z]') {
            Write-Host "  [WARNING] Skipped corrupted ID (encoding issue): $id" -ForegroundColor Yellow
            continue
        }
        # Accept proper package IDs (with Unicode support for Chinese publishers)
        if ($id -match '^\S+\.\S+$') {
            $packages += $id
        }
    }
}

Write-Log "Found $($packages.Count) packages to process"

if ($packages.Count -eq 0) {
    Write-Log "ERROR: No packages found. Check winget availability."
    exit 1
}

# Limit packages if MaxPackages is set
if ($MaxPackages -gt 0 -and $packages.Count -gt $MaxPackages) {
    Write-Log "Limiting to first $MaxPackages packages for testing"
    $packages = $packages | Select-Object -First $MaxPackages
}

# === PASS 1: Collect all tags for correlation analysis ===
Write-Log ""
Write-Log "=== PASS 1: Collecting tags for correlation analysis ==="
$allPackageTags = @()
$pass1Start = Get-Date

for ($i = 0; $i -lt $packages.Count; $i++) {
    $package = $packages[$i]
    
    if (($i + 1) % 100 -eq 0) {
        $msg = "  Collecting tags: $($i + 1)/$($packages.Count)"
        Write-Host $msg -ForegroundColor Yellow
        Write-Log $msg
    }
    
    try {
        $tags = Get-PackageTags $package
        if ($tags.Count -gt 0) {
            $allPackageTags += ,@($tags)
        }
    } catch {
        Write-Log "  Warning: Failed to get tags for $package"
    }
    
    Start-Sleep -Milliseconds 50
}

$pass1Time = (Get-Date) - $pass1Start
Write-Log "Pass 1 complete: Collected tags from $($packages.Count) packages in $($pass1Time.ToString('mm\:ss'))"

# Build correlation map and inference rules
$inferenceRules = Build-CorrelationMap $allPackageTags
Write-Log "Generated $($inferenceRules.Keys.Count) inference rules"
Write-Log ""

# === PASS 2: Process packages with inference rules ===
Write-Log "=== PASS 2: Processing packages with inference rules ==="
$processed = 0
$successful = 0
$failed = 0
$pass2Start = Get-Date

for ($i = 0; $i -lt $packages.Count; $i++) {
    $package = $packages[$i]
    $processed++
    
    Write-Host "[$processed/$($packages.Count)] Processing: $package" -ForegroundColor Cyan
    
    if (Process-Package $package $inferenceRules) {
        $successful++
    } else {
        $failed++
    }
    
    # Progress update every batch
    if ($processed % $BatchSize -eq 0) {
        $elapsed = (Get-Date) - $pass2Start
        $rate = $processed / $elapsed.TotalSeconds
        $remaining = ($packages.Count - $processed) / $rate
        
        Write-Log "Progress: $processed/$($packages.Count) | Success: $successful | Failed: $failed | ETA: $([math]::Round($remaining/60, 1)) minutes"
    }
    
    # Small delay to avoid overwhelming winget
    Start-Sleep -Milliseconds 100
}

$scriptEndTime = Get-Date
$totalTime = $scriptEndTime - $startTime
$scriptTotalTime = $scriptEndTime - $scriptStartTime

Write-Log "=== COMPLETE ==="
Write-Log "Script started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Log "Script ended: $($scriptEndTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Log ""
Write-Log "Total packages: $($packages.Count)"
Write-Log "Successful: $successful"
Write-Log "Failed: $failed"
Write-Log ""
Write-Log "Pass 1 time: $($pass1Time.ToString('hh\:mm\:ss'))"
Write-Log "Pass 2 time: $((Get-Date) - $pass2Start | ForEach-Object {$_.ToString('hh\:mm\:ss')})"
Write-Log "Processing time: $($totalTime.ToString('hh\:mm\:ss'))"
Write-Log "Script total time: $($scriptTotalTime.ToString('hh\:mm\:ss'))"
Write-Log ""
Write-Log "Database ready: $dbPath"

# Show category summary
Write-Log ""
Write-Log "=== CATEGORY SUMMARY ==="
$categorySummary = & sqlite3\sqlite3.exe $dbPath "SELECT c.category_name, COUNT(ac.app_id) as count FROM categories c LEFT JOIN app_categories ac ON c.id = ac.category_id GROUP BY c.category_name ORDER BY count DESC;"
$categorySummary | ForEach-Object { Write-Log $_ }

Write-Log ""
Write-Log "Done! Check WinProgramManager.db for results."
