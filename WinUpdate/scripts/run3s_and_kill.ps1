$exe = (Resolve-Path .\build\WinUpdate.exe).Path
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 3
if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output 'RunComplete'
