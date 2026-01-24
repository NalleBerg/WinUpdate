# Complete WinGet Database Builder - Everything in One Swoop
# Collects: basic info, tags, categories, metadata, icons - ALL AT ONCE

param(
    [int]$MaxPackages = 0,
    [switch]$TestOnly,  # Shows 10 packages on CLI without writing to DB
    [switch]$SkipProcessed
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

$scriptStartTime = Get-Date
$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$logPath = Join-Path $scriptDir "winget_everything.log"

# Log function
function Write-Log {
    param([string]$Message, [string]$Color = "White")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Write-Host $logMessage -ForegroundColor $Color
    if (-not $TestOnly) {
        Add-Content -Path $logPath -Value $logMessage
    }
}

if ($TestOnly) {
    Write-Host "`n=== TEST MODE: Will display 10 packages with ALL data (no DB writes) ===" -ForegroundColor Yellow
    Write-Host "If this looks good, run without -TestOnly flag`n" -ForegroundColor Yellow
} else {
    Write-Log "=== COMPLETE DATABASE BUILDER STARTED ===" "Cyan"
    Write-Log "Started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))" "Cyan"
    Write-Log "Database: $dbPath"
    if ($MaxPackages -gt 0) { Write-Log "Max Packages: $MaxPackages (LIMITED)" }
}

# Initialize ignore database
function Initialize-IgnoreDatabase {
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
    
    # Backup existing ignore database before removing
    if (Test-Path $ignoreDbPath) {
        $backupPath = $ignoreDbPath -replace '\.db$', "_backup_$(Get-Date -Format 'yyyyMMdd_HHmmss').db"
        Copy-Item $ignoreDbPath $backupPath -Force
        Write-Log "Backed up ignore database to: $backupPath" "Yellow"
        Remove-Item $ignoreDbPath -Force
    }
    
    & sqlite3\sqlite3.exe $ignoreDbPath "CREATE TABLE ignored_tags (tag_name TEXT PRIMARY KEY);"
    foreach ($tag in $ignoredTags) {
        & sqlite3\sqlite3.exe $ignoreDbPath "INSERT OR IGNORE INTO ignored_tags (tag_name) VALUES ('$tag');"
    }
    
    Write-Log "Ignore database initialized with $($ignoredTags.Count) tags" "Green"
}

# Initialize main database
function Initialize-MainDatabase {
    Write-Log "Creating fresh database with complete schema..." "Cyan"
    
    # Backup existing main database before removing
    if (Test-Path $dbPath) {
        $backupPath = $dbPath -replace '\.db$', "_backup_$(Get-Date -Format 'yyyyMMdd_HHmmss').db"
        Copy-Item $dbPath $backupPath -Force
        Write-Log "Backed up main database to: $backupPath" "Yellow"
        Remove-Item $dbPath -Force
    }
    
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
    & sqlite3\sqlite3.exe $dbPath "CREATE TABLE app_categories (app_id INTEGER, category_id INTEGER, PRIMARY KEY (app_id, category_id));"
    & sqlite3\sqlite3.exe $dbPath "CREATE INDEX idx_package_id ON apps(package_id);"
    
    Write-Log "Database created successfully" "Green"
}

# Check if tag is ignored
function Test-IgnoredTag {
    param([string]$Tag)
    $result = & sqlite3\sqlite3.exe $ignoreDbPath "SELECT COUNT(*) FROM ignored_tags WHERE tag_name = '$($Tag.ToLower())';"
    return $result -eq "1"
}

# Collect ALL data for one package
function Get-CompletePackageData {
    param([string]$PackageId)
    
    $data = @{
        package_id = $PackageId
        name = $null
        version = $null
        publisher = $null
        description = $null
        tags = @()
        categories = @()
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
        icon_hex = $null
        icon_type = $null
    }
    
    $maxRetries = 3
    $retryCount = 0
    $output = $null
    
    while ($retryCount -lt $maxRetries -and [string]::IsNullOrWhiteSpace($output)) {
        try {
            Write-Log "  [VERBOSE] Calling winget show for $PackageId (attempt $($retryCount + 1)/$maxRetries)..." "Gray"
            # Filter out winget loading animation characters (-, \, /, |)
            $rawOutput = winget show $PackageId --accept-source-agreements 2>$null | Where-Object { $_.Trim() -notmatch '^[-\\/|]$' }
            $output = $rawOutput | Out-String
            
            if ([string]::IsNullOrWhiteSpace($output)) {
                $retryCount++
                if ($retryCount -lt $maxRetries) {
                    Write-Log "  [WARNING] Empty output, retrying in 2 seconds..." "Yellow"
                    Start-Sleep -Seconds 2
                }
            }
        } catch {
            $retryCount++
            Write-Log "  [ERROR] Exception: $($_.Exception.Message)" "Red"
            if ($retryCount -lt $maxRetries) {
                Write-Log "  [WARNING] Retrying in 2 seconds..." "Yellow"
                Start-Sleep -Seconds 2
            }
        }
    }
    
    if ([string]::IsNullOrWhiteSpace($output)) {
        Write-Log "  [ERROR] Failed to get package info after $maxRetries attempts" "Red"
        return $null
    }
    
    try {
        # Extract basic info
        if ($output -match 'Found (.+?) \[') { $data.name = $matches[1].Trim() }
        if ($output -match 'Version:\s+(.+)') { $data.version = $matches[1].Trim() }
        if ($output -match 'Publisher:\s+(.+)') { $data.publisher = $matches[1].Trim() }
        if ($output -match 'Description:\s+(.+)') { $data.description = $matches[1].Trim() }
        
        # Extract metadata
        if ($output -match 'Homepage:\s+(.+)') { $data.homepage = $matches[1].Trim() }
        if ($output -match 'Publisher Url:\s+(.+)') { $data.publisher_url = $matches[1].Trim() }
        if ($output -match 'Publisher Support Url:\s+(.+)') { $data.publisher_support_url = $matches[1].Trim() }
        if ($output -match 'Author:\s+(.+)') { $data.author = $matches[1].Trim() }
        if ($output -match 'License:\s+(.+)') { $data.license = $matches[1].Trim() }
        if ($output -match 'License Url:\s+(.+)') { $data.license_url = $matches[1].Trim() }
        if ($output -match 'Privacy Url:\s+(.+)') { $data.privacy_url = $matches[1].Trim() }
        if ($output -match 'Copyright:\s+(.+)') { $data.copyright = $matches[1].Trim() }
        if ($output -match 'Copyright Url:\s+(.+)') { $data.copyright_url = $matches[1].Trim() }
        if ($output -match 'Release Notes Url:\s+(.+)') { $data.release_notes_url = $matches[1].Trim() }
        if ($output -match 'Moniker:\s+(.+)') { $data.moniker = $matches[1].Trim() }
        if ($output -match 'Release Date:\s+(.+)') { $data.release_date = $matches[1].Trim() }
        
        # Extract tags
        if ($output -match 'Tags:\s+(.+)') {
            $tagsLine = $matches[1].Trim()
            $rawTags = $tagsLine -split ',' | ForEach-Object { $_.Trim() }
            
            foreach ($tag in $rawTags) {
                if ($tag -match '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
                    if (-not (Test-IgnoredTag $tag)) {
                        $data.tags += $tag.ToLower()
                        # Capitalize first letter for category
                        $catName = $tag.Substring(0,1).ToUpper() + $tag.Substring(1).ToLower()
                        if ($data.categories -notcontains $catName) {
                            $data.categories += $catName
                        }
                    }
                }
            }
        }
        
        Write-Log "  [VERBOSE] Found $($data.tags.Count) valid tags" "Gray"
        
        # Download icon if we have homepage
        if ($data.homepage) {
            Write-Log "  [VERBOSE] Attempting icon download from $($data.homepage)..." "Gray"
            
            try {
                $uri = [System.Uri]$data.homepage
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
                            $data.icon_hex = ($bytes | ForEach-Object { $_.ToString("X2") }) -join ''
                            
                            if ($bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50) { $data.icon_type = 'png' }
                            elseif ($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xD8) { $data.icon_type = 'jpg' }
                            else { $data.icon_type = 'ico' }
                            
                            Write-Log "  [VERBOSE] Icon downloaded: $($data.icon_type), $($bytes.Length) bytes" "Gray"
                            break
                        }
                    } catch {
                        Write-Log "  [WARNING] Failed to download from $faviconUrl : $($_.Exception.Message)" "Yellow"
                        continue
                    }
                }
            } catch {
                Write-Log "  [WARNING] Icon download failed" "Yellow"
            }
        }
        
        return $data
        
    } catch {
        Write-Log "  [ERROR] Exception: $($_.Exception.Message)" "Red"
        return $null
    }
}

# Display package data on console (for test mode)
function Show-PackageData {
    param($data)
    
    Write-Host "`n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Cyan
    Write-Host "Package ID:     $($data.package_id)" -ForegroundColor White
    Write-Host "Name:           $($data.name)" -ForegroundColor White
    Write-Host "Version:        $($data.version)" -ForegroundColor White
    Write-Host "Publisher:      $($data.publisher)" -ForegroundColor White
    Write-Host "Description:    $($data.description)" -ForegroundColor Gray
    Write-Host "Categories:     $($data.categories -join ', ')" -ForegroundColor Green
    Write-Host "Homepage:       $($data.homepage)" -ForegroundColor Cyan
    Write-Host "Publisher URL:  $($data.publisher_url)" -ForegroundColor Cyan
    Write-Host "Support URL:    $($data.publisher_support_url)" -ForegroundColor Cyan
    Write-Host "Author:         $($data.author)" -ForegroundColor White
    Write-Host "License:        $($data.license)" -ForegroundColor Yellow
    Write-Host "License URL:    $($data.license_url)" -ForegroundColor Yellow
    Write-Host "Privacy URL:    $($data.privacy_url)" -ForegroundColor Cyan
    Write-Host "Copyright:      $($data.copyright)" -ForegroundColor Gray
    Write-Host "Copyright URL:  $($data.copyright_url)" -ForegroundColor Cyan
    Write-Host "Release Notes:  $($data.release_notes_url)" -ForegroundColor Magenta
    Write-Host "Moniker:        $($data.moniker)" -ForegroundColor Gray
    Write-Host "Release Date:   $($data.release_date)" -ForegroundColor Gray
    if ($data.icon_hex) {
        Write-Host "Icon:           YES ($($data.icon_type), $($data.icon_hex.Length/2) bytes)" -ForegroundColor Green
    } else {
        Write-Host "Icon:           NO" -ForegroundColor Red
    }
}

# Write package data to database
function Write-PackageToDatabase {
    param($data)
    
    $maxRetries = 3
    $retryCount = 0
    
    while ($retryCount -lt $maxRetries) {
        try {
            Write-Log "  [DB] Writing package to database (attempt $($retryCount + 1)/$maxRetries)..." "Cyan"
            
            # Escape single quotes
            $esc = @{}
            foreach ($key in $data.Keys) {
                if ($data[$key] -is [string]) {
                    $esc[$key] = $data[$key] -replace "'", "''"
                } else {
                    $esc[$key] = $data[$key]
                }
            }
        
        # Build INSERT query
        $fields = @("package_id", "name", "version", "publisher", "description")
        $values = @("'$($esc.package_id)'", "'$($esc.name)'", "'$($esc.version)'", "'$($esc.publisher)'", "'$($esc.description)'")
        
        $metaFields = @("homepage", "publisher_url", "publisher_support_url", "author", "license", "license_url", "privacy_url", "copyright", "copyright_url", "release_notes_url", "moniker", "release_date")
        foreach ($field in $metaFields) {
            if ($esc[$field]) {
                $fields += $field
                $values += "'$($esc[$field])'"
            }
        }
        
        if ($data.icon_hex) {
            $fields += @("icon_data", "icon_type")
            $values += @("X'$($data.icon_hex)'", "'$($data.icon_type)'")
        }
        
        $insertSql = "INSERT OR REPLACE INTO apps ($($fields -join ', ')) VALUES ($($values -join ', '));"
        
        # Write SQL to temp file to avoid command-line length limits
        $tempSql = Join-Path $env:TEMP "winget_insert_$($PID)_$([guid]::NewGuid().ToString('N').Substring(0,8)).sql"
        try {
            Set-Content -Path $tempSql -Value $insertSql -Encoding ASCII -ErrorAction Stop
            $result = Get-Content $tempSql | & sqlite3\sqlite3.exe $dbPath 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "SQLite returned error: $result"
            }
        } finally {
            if (Test-Path $tempSql) {
                Remove-Item $tempSql -Force -ErrorAction SilentlyContinue
            }
        }
        
        # Get app ID
        $appId = & sqlite3\sqlite3.exe $dbPath "SELECT id FROM apps WHERE package_id = '$($esc.package_id)';"
        Write-Log "  [DB] App inserted with ID: $appId" "Green"
        
        # Insert categories
        foreach ($catName in $data.categories) {
            $escCat = $catName -replace "'", "''"
            
            # Get or create category
            $catId = & sqlite3\sqlite3.exe $dbPath "SELECT id FROM categories WHERE category_name = '$escCat';"
            if ([string]::IsNullOrWhiteSpace($catId)) {
                & sqlite3\sqlite3.exe $dbPath "INSERT INTO categories (category_name) VALUES ('$escCat');"
                $catId = & sqlite3\sqlite3.exe $dbPath "SELECT last_insert_rowid();"
            }
            
            # Link app to category
            & sqlite3\sqlite3.exe $dbPath "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($appId, $catId);"
        }
        
            Write-Log "  [DB] SUCCESS - All data written" "Green"
            return $true
            
        } catch {
            $retryCount++
            Write-Log "  [DB ERROR] $($_.Exception.Message)" "Red"
            if ($retryCount -lt $maxRetries) {
                Write-Log "  [DB] Retrying in 2 seconds..." "Yellow"
                Start-Sleep -Seconds 2
            } else {
                Write-Log "  [DB] Failed after $maxRetries attempts" "Red"
                return $false
            }
        }
    }
    
    return $false
}

# ============ MAIN EXECUTION ============

# Initialize databases (unless test mode)
if (-not $TestOnly) {
    if (-not (Test-Path $ignoreDbPath)) {
        Initialize-IgnoreDatabase
    }
    # Only create database if it doesn't exist - NEVER delete existing data!
    if (-not (Test-Path $dbPath)) {
        Initialize-MainDatabase
    } else {
        Write-Log "Using existing database: $dbPath" "Green"
    }
}

# Get all winget packages
$wingetListFile = Join-Path $scriptDir "winget_package_list.txt"

if (-not (Test-Path $wingetListFile)) {
    Write-Log "ERROR: Package list file not found: $wingetListFile" "Red"
    Write-Log "" "White"
    Write-Log "Please run this command first in PowerShell:" "Yellow"
    Write-Log "  winget search . --source winget --accept-source-agreements > winget_package_list.txt" "Cyan"
    Write-Log "" "White"
    Write-Log "This will create the package list file that this script can parse." "Yellow"
    Write-Log "The winget search command may take 2-5 minutes to complete." "Yellow"
    exit 1
}

Write-Log "Reading package list from: $wingetListFile" "Cyan"
$packages = @()

$lines = Get-Content $wingetListFile -Encoding UTF8
Write-Log "Processing $($lines.Count) lines from winget output..." "Gray"

# Get existing package IDs if SkipProcessed is enabled
$existingPackages = @()
if ($SkipProcessed -and (Test-Path $dbPath)) {
    Write-Log "Loading existing package IDs to skip..." "Yellow"
    $existingPackages = & sqlite3\sqlite3.exe $dbPath "SELECT package_id FROM apps;" | ForEach-Object { $_.Trim() }
    Write-Log "Found $($existingPackages.Count) existing packages in database" "Yellow"
}

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
        # Skip obviously corrupted IDs (numbers followed by ASCII text without proper separator)
        if ($id -match '^\d+\.\d+[A-Za-z]') {
            Write-Host "  [WARNING] Skipped corrupted ID (encoding issue): $id" -ForegroundColor Yellow
            continue
        }
        # Accept proper package IDs (with Unicode support for Chinese publishers)
        if ($id -match '^\S+\.\S+$') {
            # Skip if already in database (when SkipProcessed is enabled)
            if ($SkipProcessed -and $existingPackages -contains $id) {
                continue
            }
            $packages += $id
        }
    }
}

# Filter count after SkipProcessed logic
if ($SkipProcessed) {
    Write-Log "After filtering existing: $($packages.Count) new packages to process" "Yellow"
}

if ($TestOnly) {
    $totalPackages = [Math]::Min(10, $packages.Count)
} elseif ($MaxPackages -gt 0) {
    $totalPackages = [Math]::Min($MaxPackages, $packages.Count)
} else {
    $totalPackages = $packages.Count
}

$packages = $packages | Select-Object -First $totalPackages

Write-Log "Found $totalPackages packages to process" "Green"

if ($packages.Count -eq 0) {
    Write-Log "ERROR: No packages found!" "Red"
    exit 1
}

# Process packages
$successful = 0
$failed = 0
$processStartTime = Get-Date
$packageTimes = @()

for ($i = 0; $i -lt $packages.Count; $i++) {
    $pkg = $packages[$i]
    $index = $i + 1
    $pkgStartTime = Get-Date
    
    # Calculate ETA
    $elapsedTotal = (Get-Date) - $processStartTime
    if ($index -gt 1) {
        $avgTimePerPkg = $elapsedTotal.TotalSeconds / ($index - 1)
        $remainingPkgs = $totalPackages - $index + 1
        $etaSeconds = $avgTimePerPkg * $remainingPkgs
        $etaMinutes = [math]::Round($etaSeconds / 60, 1)
        $etaHours = [math]::Round($etaSeconds / 3600, 2)
        if ($etaHours -ge 1) {
            $etaStr = "$etaHours hours"
        } else {
            $etaStr = "$etaMinutes minutes"
        }
    } else {
        $etaStr = "Calculating..."
    }
    
    Write-Log "\n========================================================" "Cyan"
    Write-Log "PACKAGE: $pkg" "White"
    Write-Log "PROGRESS: $index of $totalPackages" "Yellow"
    Write-Log "ELAPSED: $([math]::Round($elapsedTotal.TotalMinutes, 1)) min | ETA: $etaStr" "Yellow"
    Write-Log "========================================================" "Cyan"
    
    # Collect complete data
    $data = Get-CompletePackageData -PackageId $pkg
    
    $pkgElapsed = ((Get-Date) - $pkgStartTime).TotalSeconds
    
    if (-not $data) {
        Write-Log "  [FAILED] Could not collect data (took $([math]::Round($pkgElapsed, 1))s)" "Red"
        $failed++
        continue
    }
    
    if ($TestOnly) {
        # Display on console
        Show-PackageData $data
        $pkgElapsed = ((Get-Date) - $pkgStartTime).TotalSeconds
        Write-Host "  Package processed in $([math]::Round($pkgElapsed, 1)) seconds" -ForegroundColor Gray
    } else {
        # Write to database
        $result = Write-PackageToDatabase $data
        
        $pkgElapsed = ((Get-Date) - $pkgStartTime).TotalSeconds
        $packageTimes += $pkgElapsed
        
        if ($result) {
            $successful++
            Write-Log "  [SUCCESS] Written to database (took $([math]::Round($pkgElapsed, 1))s)" "Green"
        } else {
            $failed++
            Write-Log "  [FAILED] Database write failed (took $([math]::Round($pkgElapsed, 1))s)" "Red"
        }
        
        # Calculate running statistics
        $avgPkgTime = ($packageTimes | Measure-Object -Average).Average
        $successRate = [math]::Round(($successful / $index) * 100, 1)
        
        Write-Log "  Avg time/pkg: $([math]::Round($avgPkgTime, 1))s | Success rate: $successRate%" "Gray"
        
        # Progress report every 10 packages
        if ($index % 10 -eq 0) {
            $elapsed = (Get-Date) - $processStartTime
            $rate = $index / $elapsed.TotalSeconds
            $remaining = ($totalPackages - $index) / $rate
            
            Write-Log "\n================ MILESTONE REPORT ================" "Magenta"
            Write-Log "Processed: $index/$totalPackages packages" "Magenta"
            Write-Log "Successful: $successful | Failed: $failed" "Magenta"
            Write-Log "Success Rate: $successRate%" "Magenta"
            Write-Log "Rate: $([math]::Round($rate, 2)) pkg/sec" "Magenta"
            Write-Log "Total Elapsed: $([math]::Round($elapsed.TotalMinutes, 1)) min" "Magenta"
            Write-Log "Est. Remaining: $([math]::Round($remaining / 60, 1)) min" "Magenta"
            Write-Log "==================================================\n" "Magenta"
        }
    }
}

# Final summary
if ($TestOnly) {
    Write-Host "`n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Yellow
    Write-Host "TEST COMPLETE - Displayed $totalPackages packages" -ForegroundColor Yellow
    Write-Host "If this looks good, run: .\build_everything.ps1" -ForegroundColor Yellow
    Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━`n" -ForegroundColor Yellow
} else {
    $scriptEndTime = Get-Date
    $totalTime = $scriptEndTime - $scriptStartTime
    
    Write-Log "`n━━━━━━━━━━ COMPLETE ━━━━━━━━━━" "Green"
    Write-Log "Started: $($scriptStartTime.ToString('yyyy-MM-dd HH:mm:ss'))" "Green"
    Write-Log "Ended: $($scriptEndTime.ToString('yyyy-MM-dd HH:mm:ss'))" "Green"
    Write-Log "Total time: $($totalTime.ToString('hh\:mm\:ss'))" "Green"
    Write-Log "Successful: $successful" "Green"
    Write-Log "Failed: $failed" "Green"
    Write-Log "`nDatabase: $dbPath" "Cyan"
    Write-Log "Check in HeidiSQL!" "Cyan"
    
    # Category summary
    Write-Log "`n━━━ TOP CATEGORIES ━━━" "Yellow"
    $cats = & sqlite3\sqlite3.exe $dbPath "SELECT c.category_name, COUNT(ac.app_id) as cnt FROM categories c LEFT JOIN app_categories ac ON c.id = ac.category_id GROUP BY c.category_name ORDER BY cnt DESC LIMIT 15;"
    $cats | ForEach-Object { Write-Log $_ "Yellow" }
}
