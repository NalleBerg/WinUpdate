for ($i=1; $i -le 3; $i++) {
    Write-Output "Starting run $i"
    $p = Start-Process -FilePath '.\WinUpdate\WinUpdate.exe' -PassThru
    Start-Sleep -Seconds 6
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Seconds 1
}
Write-Output '--- wup_run_log.txt ---'
if (Test-Path 'wup_run_log.txt') { Get-Content -Raw 'wup_run_log.txt' } else { Write-Output '(no log file found)' }
