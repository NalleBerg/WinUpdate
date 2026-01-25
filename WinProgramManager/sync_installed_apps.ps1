# Sync Installed Apps from winget list
# This script queries winget for installed packages and updates the database

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

Write-Host "=== Syncing Installed Apps ===" -ForegroundColor Cyan
Write-Host "Time: $timestamp" -ForegroundColor Cyan
Write-Host "Database: $dbPath`n" -ForegroundColor Cyan

# Check if database exists
if (-not (Test-Path $dbPath)) {
    Write-Host "ERROR: Database not found at $dbPath" -ForegroundColor Red
    exit 1
}

# Create installed_apps table if it doesn't exist
Write-Host "Creating installed_apps table..." -ForegroundColor Yellow
& sqlite3\sqlite3.exe $dbPath @"
CREATE TABLE IF NOT EXISTS installed_apps (
    package_id TEXT PRIMARY KEY,
    installed_date TEXT,
    last_seen TEXT,
    installed_version TEXT,
    source TEXT,
    FOREIGN KEY (package_id) REFERENCES apps(package_id)
);
CREATE INDEX IF NOT EXISTS idx_installed_last_seen ON installed_apps(last_seen);
"@

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to create table" -ForegroundColor Red
    exit 1
}
Write-Host "Table ready`n" -ForegroundColor Green

# Get installed packages from winget
Write-Host "Querying winget for installed packages..." -ForegroundColor Yellow
Write-Host "(This may take a moment)`n" -ForegroundColor Gray

$wingetOutput = & winget list --accept-source-agreements 2>&1 | Out-String
$lines = $wingetOutput -split "`r?`n"

# Parse winget list output
# Format: Name  Id  Version  Available  Source
$installedPackages = @()
$headerFound = $false
$nameStart = -1
$idStart = -1
$versionStart = -1
$sourceStart = -1

foreach ($line in $lines) {
    # Find header line
    if ($line -match '^Name\s+Id\s+Version') {
        $headerFound = $true
        $nameStart = $line.IndexOf('Name')
        $idStart = $line.IndexOf('Id')
        $versionStart = $line.IndexOf('Version')
        $availableIdx = $line.IndexOf('Available')
        $sourceStart = $line.IndexOf('Source')
        if ($Verbose) {
            Write-Host "Header found: Name=$nameStart Id=$idStart Version=$versionStart Source=$sourceStart" -ForegroundColor Gray
        }
        continue
    }
    
    # Skip until header found
    if (-not $headerFound) { continue }
    
    # Skip separator line
    if ($line -match '^-+\s+-+') { continue }
    
    # Skip empty lines
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    
    # Skip lines that don't have proper structure
    if ($line.Length -lt $sourceStart) { continue }
    
    # Extract fields using column positions
    try {
        $id = $line.Substring($idStart, $versionStart - $idStart).Trim()
        $version = $line.Substring($versionStart, $sourceStart - $versionStart).Trim()
        $source = $line.Substring($sourceStart).Trim()
        
        # Clean up version (remove "Available" column if present)
        if ($version -match '^\s*(\S+)') {
            $version = $matches[1]
        }
        
        # Skip if no ID
        if ([string]::IsNullOrWhiteSpace($id)) { continue }
        
        # Skip non-winget sources unless verbose
        if ($source -notmatch 'winget' -and -not $Verbose) { continue }
        
        $installedPackages += @{
            Id = $id
            Version = $version
            Source = $source
        }
        
        if ($Verbose) {
            Write-Host "Found: $id | $version | $source" -ForegroundColor Gray
        }
    }
    catch {
        if ($Verbose) {
            Write-Host "Skipped line: $line" -ForegroundColor DarkGray
        }
    }
}

Write-Host "`nFound $($installedPackages.Count) installed packages from winget" -ForegroundColor Green

if ($installedPackages.Count -eq 0) {
    Write-Host "WARNING: No packages found. Check winget installation." -ForegroundColor Yellow
    exit 0
}

# Update database
Write-Host "`nUpdating database..." -ForegroundColor Yellow

$updated = 0
$added = 0
$currentTime = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

foreach ($pkg in $installedPackages) {
    $packageId = $pkg.Id -replace "'", "''"  # Escape single quotes
    $version = $pkg.Version -replace "'", "''"
    $source = $pkg.Source -replace "'", "''"
    
    # Check if package exists in installed_apps
    $exists = & sqlite3\sqlite3.exe $dbPath "SELECT COUNT(*) FROM installed_apps WHERE package_id = '$packageId';"
    
    if ($exists -eq "1") {
        # Update existing record
        & sqlite3\sqlite3.exe $dbPath "UPDATE installed_apps SET last_seen = '$currentTime', installed_version = '$version', source = '$source' WHERE package_id = '$packageId';"
        $updated++
    }
    else {
        # Insert new record
        & sqlite3\sqlite3.exe $dbPath "INSERT INTO installed_apps (package_id, installed_date, last_seen, installed_version, source) VALUES ('$packageId', '$currentTime', '$currentTime', '$version', '$source');"
        $added++
    }
}

Write-Host "`nResults:" -ForegroundColor Cyan
Write-Host "  Added: $added" -ForegroundColor Green
Write-Host "  Updated: $updated" -ForegroundColor Green

# Remove packages that haven't been seen in this scan (uninstalled)
Write-Host "`nRemoving uninstalled packages..." -ForegroundColor Yellow
$removed = & sqlite3\sqlite3.exe $dbPath "DELETE FROM installed_apps WHERE last_seen != '$currentTime'; SELECT changes();"
Write-Host "  Removed: $removed`n" -ForegroundColor Yellow

# Show summary
$totalInstalled = & sqlite3\sqlite3.exe $dbPath "SELECT COUNT(*) FROM installed_apps;"
Write-Host "Total installed apps tracked: $totalInstalled" -ForegroundColor Cyan

Write-Host "`n=== Sync Complete ===" -ForegroundColor Green
