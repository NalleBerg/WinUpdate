$pkg = Get-AppxPackage -Name Microsoft.DesktopAppInstaller -ErrorAction SilentlyContinue
if (-not $pkg) { Write-Host 'Microsoft.DesktopAppInstaller not found'; exit 0 }
$pkg | Select-Object Name,PackageFullName,InstallLocation | Format-List
$loc = $pkg.InstallLocation
Write-Host "InstallLocation=$loc"
if (Test-Path -Path $loc) {
    Write-Host 'InstallLocation exists'
    Get-ChildItem -LiteralPath $loc -Force | Select-Object -First 20 | Format-List
} else {
    Write-Host 'InstallLocation missing or access denied'
}
$manifest = Join-Path $loc 'AppXManifest.xml'
Write-Host "Manifest path=$manifest"
if (Test-Path -Path $manifest) {
    Write-Host 'Manifest found — attempting Register'
    try {
        Add-AppxPackage -DisableDevelopmentMode -Register $manifest
        Write-Host 'Register succeeded'
    } catch {
        Write-Host 'Register failed:' $_.Exception.Message
    }
} else {
    Write-Host 'Manifest not found — cannot register'
}
