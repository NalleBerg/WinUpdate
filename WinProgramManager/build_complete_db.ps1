# Complete WinGet Database Builder - Single Pass
# Processes packages with tags, categories, metadata, and icons all at once

param(
    [int]$MaxPackages = 0,
    [int]$BatchSize = 10,
    [switch]$SkipProcessed
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

$scriptStartTime = Get-Date
Write-Host "=== Complete Database Builder Started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss')) ===" -ForegroundColor Cyan

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$logPath = Join-Path $scriptDir "winget_complete_import.log"

# Log function
function Write-Log {
    param([string]$Message)
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Write-Host $logMessage
    Add-Content -Path $logPath -Value $logMessage
}

Write-Log "=== Complete Database Builder Started ==="
Write-Log "Database: $dbPath"
Write-Log "Batch Size: $BatchSize"
if ($MaxPackages -gt 0) { Write-Log "Max Packages: $MaxPackages (TEST MODE)" }
if ($SkipProcessed) { Write-Log "Skip Processed: ENABLED" }

# Initialize ignore database with comprehensive tag list
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
        'chromium', 'gecko', 'webkit', 'blink', 'trident', 'ie', 'internet-explorer',
        'docker', 'kubernetes', 'k8s', 'container',
        'nginx', 'apache', 'iis',
        'mysql', 'postgresql', 'mongodb', 'redis',
        'react', 'vue', 'angular', 'svelte',
        'bootstrap', 'tailwind', 'material',
        'typescript', 'javascript', 'js', 'ts',
        'cpp', 'c++', 'csharp', 'c#',
        'rust', 'go', 'golang', 'ruby', 'php', 'perl',
        'html', 'css', 'xml', 'json', 'yaml',
        'api', 'rest', 'graphql', 'soap',
        'git', 'svn', 'mercurial',
        'ssh', 'ftp', 'sftp', 'http', 'https',
        'tcp', 'udp', 'ipv4', 'ipv6',
        'ssl', 'tls', 'vpn',
        'wireguard', 'openvpn', 'shadowsocks', 'v2ray', 'trojan',
        'chatgpt-app', 'copilot', 'ai-assistant',
        'application', 'software', 'tool', 'utility', 'program',
        'app', 'client', 'server', 'service',
        'manager', 'viewer', 'editor', 'creator',
        'installer', 'launcher', 'downloader',
        'framework', 'library', 'sdk',
        'interface', 'ui', 'ux',
        'website', 'webpage', 'site',
        'english', 'chinese', 'spanish', 'french', 'german',
        'multilingual', 'localization', 'translation'
    )
    
    if (Test-Path $ignoreDbPath) {
        Remove-Item $ignoreDbPath -Force
    }
    
    & sqlite3\sqlite3.exe $ignoreDbPath "CREATE TABLE ignored_tags (tag_name TEXT PRIMARY KEY);"
    
    foreach ($tag in $ignoredTags) {
        & sqlite3\sqlite3.exe $ignoreDbPath "INSERT OR IGNORE INTO ignored_tags (tag_name) VALUES ('$tag');"
    }
    
    Write-Log "Ignore database initialized with $($ignoredTags.Count) tags"
}

# Initialize main database with ALL columns
function Initialize-MainDatabase {
    Write-Log "Initializing main database with complete schema..."
    
    if (Test-Path $dbPath) {
        Remove-Item $dbPath -Force
    }
    
    # Apps table with ALL metadata columns
    $createApps = @"
CREATE TABLE apps (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id TEXT UNIQUE NOT NULL,
    name TEXT,
    version TEXT,
    publisher TEXT,
    description TEXT,
    homepage TEXT,
    publisher_url TEXT,
    publisher_support_url TEXT,
    author TEXT,
    license TEXT,
    license_url TEXT,
    privacy_url TEXT,
    copyright TEXT,
    copyright_url TEXT,
    release_notes_url TEXT,
    moniker TEXT,
    release_date TEXT,
    icon_data BLOB,
    icon_type TEXT,
    processed_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
"@
    
    & sqlite3\sqlite3.exe $dbPath $createApps
    & sqlite3\sqlite3.exe $dbPath "CREATE TABLE categories (id INTEGER PRIMARY KEY AUTOINCREMENT, category_name TEXT UNIQUE NOT NULL);"
    & sqlite3\sqlite3.exe $dbPath "CREATE TABLE app_categories (app_id INTEGER NOT NULL, category_id INTEGER NOT NULL, PRIMARY KEY (app_id, category_id), FOREIGN KEY (app_id) REFERENCES apps(id), FOREIGN KEY (category_id) REFERENCES categories(id));"
    & sqlite3\sqlite3.exe $dbPath "CREATE INDEX idx_package_id ON apps(package_id);"
    & sqlite3\sqlite3.exe $dbPath "CREATE INDEX idx_category_name ON categories(category_name);"
    
    Write-Log "Main database initialized with complete schema"
}

# Check if tag is ignored
function Test-IgnoredTag {
    param([string]$Tag)
    $result = & sqlite3\sqlite3.exe $ignoreDbPath "SELECT COUNT(*) FROM ignored_tags WHERE tag_name = '$($Tag.ToLower())';"
    return $result -eq "1"
}

# Get or create category ID
function Get-CategoryId {
    param([string]$CategoryName)
    
    # Capitalize first letter
    $capitalized = $CategoryName.Substring(0,1).ToUpper() + $CategoryName.Substring(1).ToLower()
    
    $existing = & sqlite3\sqlite3.exe $dbPath "SELECT id FROM categories WHERE category_name = '$capitalized';"
    if ($existing) {
        return $existing
    }
    
    & sqlite3\sqlite3.exe $dbPath "INSERT INTO categories (category_name) VALUES ('$capitalized');"
    return & sqlite3\sqlite3.exe $dbPath "SELECT last_insert_rowid();"
}

# Get package tags
function Get-PackageTags {
    param([string]$PackageId)
    
    $output = winget show $PackageId --accept-source-agreements 2>$null | Out-String
    
    if ($output -match 'Tags:\s+(.+)') {
        $tagsLine = $matches[1].Trim()
        $tags = $tagsLine -split ',' | ForEach-Object {
            $tag = $_.Trim()
            # Only accept ASCII alphanumeric tags
            if ($tag -match '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
                if (-not (Test-IgnoredTag $tag)) {
                    $tag.ToLower()
                }
            }
        } | Where-Object { $_ }
        
        return $tags
    }
    
    return @()
}

# Get complete package metadata
function Get-PackageMetadata {
    param([string]$PackageId)
    
    $metadata = @{
        homepage = $null
        publisher_url = $null
        publisher_support_url = $null
        author = $null
        license = $null
        license_url = $null
        privacy_url = $null
        copyright = $null
        copyright_url = $null
        release_notes_url = $null
        moniker = $null
        release_date = $null
    }
    
    try {
        $output = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        if ($output -match 'Homepage:\s+(.+)') { $metadata.homepage = $matches[1].Trim() }
        if ($output -match 'Publisher Url:\s+(.+)') { $metadata.publisher_url = $matches[1].Trim() }
        if ($output -match 'Publisher Support Url:\s+(.+)') { $metadata.publisher_support_url = $matches[1].Trim() }
        if ($output -match 'Author:\s+(.+)') { $metadata.author = $matches[1].Trim() }
        if ($output -match 'License:\s+(.+)') { $metadata.license = $matches[1].Trim() }
        if ($output -match 'License Url:\s+(.+)') { $metadata.license_url = $matches[1].Trim() }
        if ($output -match 'Privacy Url:\s+(.+)') { $metadata.privacy_url = $matches[1].Trim() }
        if ($output -match 'Copyright:\s+(.+)') { $metadata.copyright = $matches[1].Trim() }
        if ($output -match 'Copyright Url:\s+(.+)') { $metadata.copyright_url = $matches[1].Trim() }
        if ($output -match 'Release Notes Url:\s+(.+)') { $metadata.release_notes_url = $matches[1].Trim() }
        if ($output -match 'Moniker:\s+(.+)') { $metadata.moniker = $matches[1].Trim() }
        if ($output -match 'Release Date:\s+(.+)') { $metadata.release_date = $matches[1].Trim() }
        
    } catch {
        Write-Log "  Warning: Failed to get full metadata"
    }
    
    return $metadata
}

# Download package icon
function Get-PackageIcon {
    param([string]$HomepageUrl)
    
    if (-not $HomepageUrl) { return $null }
    
    try {
        $uri = [System.Uri]$HomepageUrl
        $domain = $uri.Host
        
        $faviconUrls = @(
            "https://$domain/favicon.ico",
            "https://www.google.com/s2/favicons?domain=$domain&sz=64"
        )
        
        foreach ($faviconUrl in $faviconUrls) {
            try {
                $response = Invoke-WebRequest -Uri $faviconUrl -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
                $bytes = $response.Content
                
                if ($bytes.Length -gt 100) {
                    $hexString = ($bytes | ForEach-Object { $_.ToString("X2") }) -join ''
                    
                    $iconType = 'ico'
                    if ($bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50) { $iconType = 'png' }
                    elseif ($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xD8) { $iconType = 'jpg' }
                    
                    return @{ hex = $hexString; type = $iconType }
                }
            } catch {
                continue
            }
        }
    } catch {
        return $null
    }
    
    return $null
}

# Process single package completely
function Process-Package {
    param([string]$PackageId, [int]$Index, [int]$Total)
    
    Write-Log "[$Index/$Total] Processing: $PackageId"
    
    try {
        # Get basic info
        $info = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        $name = if ($info -match 'Found (.+) \[') { $matches[1].Trim() } else { $PackageId }
        $version = if ($info -match 'Version:\s+(.+)') { $matches[1].Trim() } else { "" }
        $publisher = if ($info -match 'Publisher:\s+(.+)') { $matches[1].Trim() } else { "" }
        $description = if ($info -match 'Description:\s+(.+)') { $matches[1].Trim() } else { "" }
        
        # Get tags
        $tags = Get-PackageTags -PackageId $PackageId
        Write-Log "  Tags: $($tags -join ', ')"
        
        # Get metadata
        $metadata = Get-PackageMetadata -PackageId $PackageId
        
        # Get icon
        $iconData = $null
        $iconType = $null
        if ($metadata.homepage) {
            Write-Log "  Downloading icon..."
            $icon = Get-PackageIcon -HomepageUrl $metadata.homepage
            if ($icon) {
                $iconData = $icon.hex
                $iconType = $icon.type
                Write-Log "  Icon downloaded ($iconType)"
            }
        }
        
        # Escape single quotes for SQL
        $escapedId = $PackageId -replace "'", "''"
        $escapedName = $name -replace "'", "''"
        $escapedVersion = $version -replace "'", "''"
        $escapedPublisher = $publisher -replace "'", "''"
        $escapedDescription = $description -replace "'", "''"
        
        # Build INSERT query with all fields
        $insertFields = @("package_id", "name", "version", "publisher", "description")
        $insertValues = @("'$escapedId'", "'$escapedName'", "'$escapedVersion'", "'$escapedPublisher'", "'$escapedDescription'")
        
        foreach ($key in $metadata.Keys) {
            if ($metadata[$key]) {
                $insertFields += $key
                $escapedValue = $metadata[$key] -replace "'", "''"
                $insertValues += "'$escapedValue'"
            }
        }
        
        if ($iconData) {
            $insertFields += @("icon_data", "icon_type")
            $insertValues += @("X'$iconData'", "'$iconType'")
        }
        
        $insertQuery = "INSERT OR REPLACE INTO apps ($($insertFields -join ', ')) VALUES ($($insertValues -join ', '));"
        & sqlite3\sqlite3.exe $dbPath $insertQuery
        
        # Get app ID
        $appId = & sqlite3\sqlite3.exe $dbPath "SELECT id FROM apps WHERE package_id = '$escapedId';"
        
        # Insert categories
        foreach ($tag in $tags) {
            $catId = Get-CategoryId -CategoryName $tag
            & sqlite3\sqlite3.exe $dbPath "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($appId, $catId);"
        }
        
        Write-Log "  SUCCESS: Saved to database (ID: $appId)"
        return $true
        
    } catch {
        Write-Log "  ERROR: $($_.Exception.Message)"
        return $false
    }
}

# Initialize databases
if (-not (Test-Path $ignoreDbPath)) {
    Initialize-IgnoreDatabase
}
if (-not (Test-Path $dbPath) -or -not $SkipProcessed) {
    Initialize-MainDatabase
}

# Get all winget packages
Write-Log "Fetching winget package list..."
$packages = @()

$searchOutput = winget search . --source winget --accept-source-agreements 2>&1 | Out-String
$lines = $searchOutput -split "`n"

$inData = $false
foreach ($line in $lines) {
    if ($line -match '^-{5,}') {
        $inData = $true
        continue
    }
    
    if (-not $inData -or [string]::IsNullOrWhiteSpace($line.Trim())) {
        continue
    }
    
    $parts = $line -split '\s{2,}' | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
    
    if ($parts.Count -ge 2) {
        $id = $parts[1].Trim()
        if ($id -match '^[A-Za-z0-9][\w._-]*\.[\w._-]+$') {
            $packages += $id
        }
    }
}

$totalPackages = if ($MaxPackages -gt 0) { [Math]::Min($MaxPackages, $packages.Count) } else { $packages.Count }
$packages = $packages | Select-Object -First $totalPackages

Write-Log "Found $totalPackages packages to process"

if ($packages.Count -eq 0) {
    Write-Log "ERROR: No packages found!"
    exit 1
}

# Process packages
$successful = 0
$failed = 0
$skipped = 0
$processStartTime = Get-Date

for ($i = 0; $i -lt $packages.Count; $i++) {
    $pkg = $packages[$i]
    $index = $i + 1
    
    # Check if already processed
    if ($SkipProcessed) {
        $escapedPkg = $pkg -replace "'", "''"
        $existing = & sqlite3\sqlite3.exe $dbPath "SELECT id FROM apps WHERE package_id = '$escapedPkg';"
        if ($existing) {
            Write-Log "[$index/$totalPackages] Skipping (already processed): $pkg"
            $skipped++
            continue
        }
    }
    
    $result = Process-Package -PackageId $pkg -Index $index -Total $totalPackages
    
    if ($result) {
        $successful++
    } else {
        $failed++
    }
    
    # Progress report every 10 packages
    if ($index % 10 -eq 0) {
        $elapsed = (Get-Date) - $processStartTime
        $rate = $successful / $elapsed.TotalSeconds
        $remaining = ($totalPackages - $index) / $rate
        
        Write-Log ""
        Write-Log "=== PROGRESS REPORT ==="
        Write-Log "Processed: $index/$totalPackages"
        Write-Log "Successful: $successful"
        Write-Log "Failed: $failed"
        Write-Log "Skipped: $skipped"
        Write-Log "Rate: $([math]::Round($rate, 2)) packages/sec"
        Write-Log "Est. remaining: $([math]::Round($remaining / 60, 1)) minutes"
        Write-Log ""
    }
}

# Final summary
$scriptEndTime = Get-Date
$totalTime = $scriptEndTime - $scriptStartTime

Write-Log ""
Write-Log "=== COMPLETE ==="
Write-Log "Script started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Log "Script ended: $($scriptEndTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Log ""
Write-Log "Total packages: $totalPackages"
Write-Log "Successful: $successful"
Write-Log "Failed: $failed"
Write-Log "Skipped: $skipped"
Write-Log "Total time: $($totalTime.ToString('hh\:mm\:ss'))"
Write-Log ""
Write-Log "Database ready: $dbPath"

# Show category summary
Write-Log ""
Write-Log "=== CATEGORY SUMMARY ==="
$categorySummary = & sqlite3\sqlite3.exe $dbPath "SELECT c.category_name, COUNT(ac.app_id) as count FROM categories c LEFT JOIN app_categories ac ON c.id = ac.category_id GROUP BY c.category_name ORDER BY count DESC LIMIT 20;"
$categorySummary | ForEach-Object { Write-Log $_ }

Write-Log ""
Write-Log "Done! Check database in HeidiSQL."
