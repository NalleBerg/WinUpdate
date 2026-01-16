param(
    [int]$MaxPackages = 0,
    [int]$BatchSize = 50,
    [switch]$SkipIcons,
    [switch]$SkipProcessed  # Skip packages that already have metadata
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$dbPath = ".\WinProgramManager.db"
$logPath = ".\metadata_update.log"

# Initialize log
"=== Metadata Update Started: $(Get-Date) ===" | Out-File $logPath

Write-Host "Adding metadata columns to database..." -ForegroundColor Cyan

# Add all metadata columns
$alterCommands = @(
    "ALTER TABLE apps ADD COLUMN homepage TEXT",
    "ALTER TABLE apps ADD COLUMN publisher_url TEXT",
    "ALTER TABLE apps ADD COLUMN publisher_support_url TEXT",
    "ALTER TABLE apps ADD COLUMN author TEXT",
    "ALTER TABLE apps ADD COLUMN license TEXT",
    "ALTER TABLE apps ADD COLUMN license_url TEXT",
    "ALTER TABLE apps ADD COLUMN privacy_url TEXT",
    "ALTER TABLE apps ADD COLUMN copyright TEXT",
    "ALTER TABLE apps ADD COLUMN copyright_url TEXT",
    "ALTER TABLE apps ADD COLUMN release_notes_url TEXT",
    "ALTER TABLE apps ADD COLUMN moniker TEXT",
    "ALTER TABLE apps ADD COLUMN release_date TEXT",
    "ALTER TABLE apps ADD COLUMN icon_data BLOB",
    "ALTER TABLE apps ADD COLUMN icon_type TEXT"
)

foreach ($cmd in $alterCommands) {
    try {
        .\sqlite3\sqlite3.exe $dbPath $cmd 2>$null
    } catch {
        # Column might already exist, continue
    }
}

Write-Host "Columns added successfully" -ForegroundColor Green

# Function to extract metadata from winget show output
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
        
        # Extract each field using regex
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
        Write-Host "  Warning: Failed to get metadata for $PackageId" -ForegroundColor Yellow
    }
    
    return $metadata
}

# Function to download and store icon
function Get-PackageIcon {
    param([string]$HomepageUrl)
    
    if (-not $HomepageUrl) { return $null }
    
    try {
        $uri = [System.Uri]$HomepageUrl
        $domain = $uri.Host
        
        # Try multiple favicon URLs
        $faviconUrls = @(
            "https://$domain/favicon.ico",
            "https://www.google.com/s2/favicons?domain=$domain&sz=64"
        )
        
        foreach ($faviconUrl in $faviconUrls) {
            try {
                $response = Invoke-WebRequest -Uri $faviconUrl -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
                $bytes = $response.Content
                
                if ($bytes.Length -gt 100) {  # Valid icon should be larger than 100 bytes
                    # Convert to hex string for SQLite BLOB
                    $hexString = ($bytes | ForEach-Object { $_.ToString("X2") }) -join ''
                    
                    # Determine icon type
                    $iconType = 'ico'
                    if ($bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50) { $iconType = 'png' }
                    elseif ($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xD8) { $iconType = 'jpg' }
                    
                    return @{
                        hex = $hexString
                        type = $iconType
                    }
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

# Get all packages from database
$query = "SELECT id, package_id, name FROM apps"
if ($MaxPackages -gt 0) {
    $query += " LIMIT $MaxPackages"
}

$packages = .\sqlite3\sqlite3.exe $dbPath $query | ForEach-Object {
    $parts = $_ -split '\|'
    [PSCustomObject]@{
        Id = $parts[0]
        PackageId = $parts[1]
        Name = $parts[2]
    }
}

$totalPackages = $packages.Count
Write-Host "`nUpdating metadata for $totalPackages packages..." -ForegroundColor Cyan
if ($SkipProcessed) {
    Write-Host "SkipProcessed enabled - will skip packages that already have metadata" -ForegroundColor Yellow
}
Write-Host "Progress will be saved every $BatchSize packages`n" -ForegroundColor Gray

$processed = 0
$skipped = 0
$updated = 0
$iconsDownloaded = 0
$startTime = Get-Date

foreach ($pkg in $packages) {
    $processed++
    $progressPercent = [math]::Round(($processed / $totalPackages) * 100, 2)
    
    # Check if package already has metadata (if SkipProcessed is set)
    if ($SkipProcessed) {
        $checkQuery = "SELECT homepage FROM apps WHERE id = $($pkg.Id)"
        $existingMetadata = .\sqlite3\sqlite3.exe $dbPath $checkQuery
        
        if (-not [string]::IsNullOrWhiteSpace($existingMetadata)) {
            Write-Host "[$processed/$totalPackages - $progressPercent%] Skipping (already processed): $($pkg.Name)" -ForegroundColor Gray
            $skipped++
            continue
        }
    }
    
    Write-Host "[$processed/$totalPackages - $progressPercent%] Processing: $($pkg.Name)" -ForegroundColor Cyan
    
    # Get metadata
    $metadata = Get-PackageMetadata -PackageId $pkg.PackageId
    
    # Escape single quotes for SQL
    $escapedFields = @{}
    foreach ($key in $metadata.Keys) {
        if ($metadata[$key]) {
            $escapedFields[$key] = $metadata[$key] -replace "'", "''"
        } else {
            $escapedFields[$key] = $null
        }
    }
    
    # Build UPDATE query for metadata
    $updateParts = @()
    foreach ($key in $escapedFields.Keys) {
        if ($escapedFields[$key]) {
            $updateParts += "$key = '$($escapedFields[$key])'"
        }
    }
    
    if ($updateParts.Count -gt 0) {
        $updateQuery = "UPDATE apps SET $($updateParts -join ', ') WHERE id = $($pkg.Id)"
        .\sqlite3\sqlite3.exe $dbPath $updateQuery
        $updated++
        
        "Updated metadata for: $($pkg.PackageId)" | Out-File $logPath -Append
    }
    
    # Download and store icon if not skipped
    if (-not $SkipIcons -and $metadata.homepage) {
        Write-Host "  Downloading icon..." -ForegroundColor Gray
        $iconData = Get-PackageIcon -HomepageUrl $metadata.homepage
        
        if ($iconData) {
            $iconQuery = "UPDATE apps SET icon_data = X'$($iconData.hex)', icon_type = '$($iconData.type)' WHERE id = $($pkg.Id)"
            .\sqlite3\sqlite3.exe $dbPath $iconQuery
            $iconsDownloaded++
            Write-Host "  Icon downloaded ($($iconData.type))" -ForegroundColor Green
            "  Icon downloaded for: $($pkg.PackageId)" | Out-File $logPath -Append
        } else {
            Write-Host "  No icon available" -ForegroundColor Yellow
        }
    }
    
    # Progress update
    if ($processed % 10 -eq 0) {
        $elapsed = (Get-Date) - $startTime
        $actualProcessed = $processed - $skipped
        $rate = if ($actualProcessed -gt 0) { $actualProcessed / $elapsed.TotalSeconds } else { 0 }
        $remaining = if ($rate -gt 0) { ($totalPackages - $processed) / $rate } else { 0 }
        
        Write-Host "`nProgress: $processed/$totalPackages packages" -ForegroundColor Magenta
        Write-Host "Skipped: $skipped packages" -ForegroundColor Magenta
        Write-Host "Metadata updated: $updated packages" -ForegroundColor Magenta
        Write-Host "Icons downloaded: $iconsDownloaded" -ForegroundColor Magenta
        Write-Host "Rate: $([math]::Round($rate, 2)) packages/sec" -ForegroundColor Magenta
        Write-Host "Est. time remaining: $([math]::Round($remaining / 60, 1)) minutes`n" -ForegroundColor Magenta
    }
}

# Final summary
$totalTime = (Get-Date) - $startTime
Write-Host "`n=== Metadata Update Complete ===" -ForegroundColor Green
Write-Host "Total packages processed: $processed" -ForegroundColor Green
Write-Host "Skipped: $skipped" -ForegroundColor Green
Write-Host "Metadata updated: $updated" -ForegroundColor Green
Write-Host "Icons downloaded: $iconsDownloaded" -ForegroundColor Green
Write-Host "Total time: $($totalTime.ToString('hh\:mm\:ss'))" -ForegroundColor Green

"=== Metadata Update Complete: $(Get-Date) ===" | Out-File $logPath -Append
"Total packages: $processed" | Out-File $logPath -Append
"Skipped: $skipped" | Out-File $logPath -Append
"Metadata updated: $updated" | Out-File $logPath -Append
"Icons downloaded: $iconsDownloaded" | Out-File $logPath -Append
"Total time: $($totalTime.ToString('hh\:mm\:ss'))" | Out-File $logPath -Append
