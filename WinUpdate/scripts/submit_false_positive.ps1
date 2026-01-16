# submit_false_positive.ps1
# Helps prepare information for submitting false positive to Microsoft

$exePath = Join-Path $PSScriptRoot "..\build\WinUpdate.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Windows Defender False Positive Report" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $exePath)) {
    Write-Host "ERROR: WinUpdate.exe not found at: $exePath" -ForegroundColor Red
    Write-Host "Please build the project first." -ForegroundColor Yellow
    exit 1
}

# Get file information
$fileInfo = Get-Item $exePath
$hash = (Get-FileHash -Path $exePath -Algorithm SHA256).Hash

Write-Host "File Information:" -ForegroundColor Green
Write-Host "  Path: $exePath" -ForegroundColor White
Write-Host "  Size: $($fileInfo.Length) bytes" -ForegroundColor White
Write-Host "  SHA256: $hash" -ForegroundColor White
Write-Host "  Modified: $($fileInfo.LastWriteTime)" -ForegroundColor White
Write-Host ""

Write-Host "Steps to submit false positive to Microsoft:" -ForegroundColor Yellow
Write-Host ""
Write-Host "1. Go to: https://www.microsoft.com/en-us/wdsi/filesubmission" -ForegroundColor Cyan
Write-Host ""
Write-Host "2. Fill out the form with these details:" -ForegroundColor White
Write-Host "   - File: Upload $exePath" -ForegroundColor Gray
Write-Host "   - SHA256: $hash" -ForegroundColor Gray
Write-Host "   - Submission Type: File incorrectly detected (False positive)" -ForegroundColor Gray
Write-Host "   - Product: Windows Defender" -ForegroundColor Gray
Write-Host "   - Detection Name: [Copy from Windows Security quarantine details]" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Describe the application:" -ForegroundColor White
Write-Host '   "WinUpdate is a legitimate open-source Windows update manager' -ForegroundColor Gray
Write-Host '    that provides a GUI for the Windows Package Manager (winget).' -ForegroundColor Gray
Write-Host '    Source code available at: https://github.com/NalleBerg/WinUpdate"' -ForegroundColor Gray
Write-Host ""
Write-Host "4. Microsoft typically responds within 24-48 hours" -ForegroundColor Green
Write-Host ""

# Check if file is currently blocked
$zone = Get-Content "$exePath`:Zone.Identifier" -ErrorAction SilentlyContinue
if ($zone) {
    Write-Host "NOTE: File has Zone Identifier (downloaded from internet)" -ForegroundColor Yellow
    Write-Host "Removing Zone.Identifier..." -ForegroundColor Yellow
    Unblock-File -Path $exePath
    Write-Host "✓ File unblocked" -ForegroundColor Green
    Write-Host ""
}

Write-Host "Opening submission page in browser..." -ForegroundColor Cyan
Start-Process "https://www.microsoft.com/en-us/wdsi/filesubmission"
