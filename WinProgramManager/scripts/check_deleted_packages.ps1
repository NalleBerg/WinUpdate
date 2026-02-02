param(
    [Parameter(Mandatory=$true)]
    [string]$DatabasePath
)

$ErrorActionPreference = "Stop"

# Get script and exe directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exeDir = Split-Path -Parent $scriptDir

# Find sqlite3.exe
$sqliteExe = Join-Path $exeDir "sqlite3\sqlite3.exe"
if (-not (Test-Path $sqliteExe)) {
    # Fallback to build source location
    $buildSourceDir = Split-Path -Parent $exeDir
    $sqliteExe = Join-Path $buildSourceDir "sqlite3\sqlite3.exe"
}

if (-not (Test-Path $sqliteExe)) {
    Write-Error "sqlite3.exe not found"
    exit 1
}

Write-Host "Running comprehensive winget search (this may take a minute)..."

# Run winget search . to get ALL available packages
$allPackages = @{}
$wingetOutput = & winget search "." --accept-source-agreements --disable-interactivity 2>$null

if ($LASTEXITCODE -eq 0) {
    $lines = $wingetOutput -split "`n"
    $inDataSection = $false
    
    foreach ($line in $lines) {
        # Skip until we hit the separator line
        if ($line -match '^-+') {
            $inDataSection = $true
            continue
        }
        
        if (-not $inDataSection) { continue }
        if ($line.Trim() -eq '') { continue }
        
        # Parse line using right-to-left tokenization
        $trimmed = $line.TrimEnd()
        if ($trimmed.Length -eq 0) { continue }
        
        # Find the last 3 fields: PackageId, Version, Source
        $parts = @()
        $remaining = $trimmed
        
        # Extract from right to left
        for ($i = 0; $i -lt 3; $i++) {
            $remaining = $remaining.TrimEnd()
            if ($remaining.Length -eq 0) { break }
            
            $lastSpace = $remaining.LastIndexOf(' ')
            if ($lastSpace -eq -1) {
                $parts += $remaining
                $remaining = ''
                break
            }
            
            $part = $remaining.Substring($lastSpace + 1)
            $parts += $part
            $remaining = $remaining.Substring(0, $lastSpace)
        }
        
        [array]::Reverse($parts)
        
        if ($parts.Count -ge 1) {
            $packageId = $parts[0]
            if ($packageId -match '^[a-zA-Z]' -and $packageId -notmatch '^\d+$') {
                $allPackages[$packageId] = $true
            }
        }
    }
}

Write-Host "Found $($allPackages.Count) available packages in winget"

# Get all packages from main database that are NOT installed
Write-Host "Checking database for obsolete packages..."

$query = @"
SELECT package_id FROM apps 
WHERE package_id NOT IN (SELECT package_id FROM installed_apps);
"@

$candidatesOutput = & $sqliteExe $DatabasePath $query
$candidates = $candidatesOutput -split "`n" | Where-Object { $_.Trim() -ne '' }

Write-Host "Found $($candidates.Count) non-installed packages in database"

# Check which candidates are NOT in winget anymore
$deletedPackages = @()
foreach ($packageId in $candidates) {
    $pkgId = $packageId.Trim()
    if ($pkgId -eq '') { continue }
    
    if (-not $allPackages.ContainsKey($pkgId)) {
        $deletedPackages += $pkgId
    }
}

if ($deletedPackages.Count -gt 0) {
    Write-Host "Found $($deletedPackages.Count) packages to delete (not in winget + not installed)"
    
    # Delete the obsolete packages
    foreach ($packageId in $deletedPackages) {
        Write-Host "  Deleting: $packageId"
        
        # Delete from app_tags first (foreign key)
        $deleteTagsSql = "DELETE FROM app_tags WHERE package_id = '$($packageId.Replace("'", "''"))';"
        & $sqliteExe $DatabasePath $deleteTagsSql | Out-Null
        
        # Delete from apps
        $deleteAppSql = "DELETE FROM apps WHERE package_id = '$($packageId.Replace("'", "''"))';"
        & $sqliteExe $DatabasePath $deleteAppSql | Out-Null
    }
    
    Write-Host "Removed $($deletedPackages.Count) obsolete package(s) from database."
} else {
    Write-Host "No obsolete packages found - database is clean."
}
exit 0
