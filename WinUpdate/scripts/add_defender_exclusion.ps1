# add_defender_exclusion.ps1
# Adds Windows Defender exclusion for WinUpdate build directory
# Must be run as Administrator

param(
    [switch]$Remove
)

$buildPath = Join-Path $PSScriptRoot "..\build\WinUpdate.exe"
$buildDir = Join-Path $PSScriptRoot "..\build"

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then run this script again." -ForegroundColor Yellow
    exit 1
}

if ($Remove) {
    Write-Host "Removing exclusion for: $buildDir" -ForegroundColor Yellow
    try {
        Remove-MpPreference -ExclusionPath $buildDir -ErrorAction Stop
        Write-Host "✓ Exclusion removed successfully!" -ForegroundColor Green
    } catch {
        Write-Host "Failed to remove exclusion: $_" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "Adding Windows Defender exclusion for: $buildDir" -ForegroundColor Cyan
    try {
        Add-MpPreference -ExclusionPath $buildDir -ErrorAction Stop
        Write-Host "✓ Exclusion added successfully!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Your build directory is now excluded from Windows Defender scanning." -ForegroundColor Green
        Write-Host "This is safe for development but remember to scan releases before distribution." -ForegroundColor Yellow
    } catch {
        Write-Host "Failed to add exclusion: $_" -ForegroundColor Red
        Write-Host ""
        Write-Host "Alternative: Add exclusion manually:" -ForegroundColor Yellow
        Write-Host "1. Open Windows Security" -ForegroundColor White
        Write-Host "2. Go to Virus & threat protection" -ForegroundColor White
        Write-Host "3. Click 'Manage settings' under Virus & threat protection settings" -ForegroundColor White
        Write-Host "4. Scroll to 'Exclusions' and click 'Add or remove exclusions'" -ForegroundColor White
        Write-Host "5. Click 'Add an exclusion' → 'Folder'" -ForegroundColor White
        Write-Host "6. Browse to: $buildDir" -ForegroundColor Cyan
        exit 1
    }
}
