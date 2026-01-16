# Analyze winget package tags
# Sample a variety of popular packages and extract their tags

$packages = @(
    "7zip.7zip", "VideoLAN.VLC", "Mozilla.Firefox", "Google.Chrome", "Microsoft.Edge",
    "Microsoft.VisualStudioCode", "Git.Git", "Python.Python.3.12", "Node.js", "Docker.DockerDesktop",
    "Adobe.Acrobat.Reader.64-bit", "SumatraPDF.SumatraPDF", "TheDocumentFoundation.LibreOffice",
    "Notepad++.Notepad++", "Microsoft.PowerToys", "WinDirStat.WinDirStat", "Spotify.Spotify",
    "Discord.Discord", "Slack.Slack", "Zoom.Zoom", "OBSProject.OBSStudio", "Audacity.Audacity",
    "GIMP.GIMP", "Inkscape.Inkscape", "Blender.Blender", "HandBrake.HandBrake", "VB-Audio.Voicemeeter",
    "qBittorrent.qBittorrent", "PuTTY.PuTTY", "WinSCP.WinSCP", "FileZilla.FileZilla",
    "Oracle.VirtualBox", "VMware.WorkstationPlayer", "Valve.Steam", "EpicGames.EpicGamesLauncher",
    "TeamViewer.TeamViewer", "AnyDesk.AnyDesk", "OpenVPNTechnologies.OpenVPN", "WireGuard.WireGuard",
    "Greenshot.Greenshot", "ShareX.ShareX", "IrfanView.IrfanView", "Paint.NET", "KeePassXCTeam.KeePassXC",
    "Bitwarden.Bitwarden", "1Password.1Password", "Brave.Brave", "Opera.Opera", "Vivaldi.Vivaldi",
    "CCleaner.CCleaner", "CrystalDewWorld.CrystalDiskInfo", "CrystalDewWorld.CrystalDiskMark",
    "MediaArea.MediaInfo", "MediaInfo.MediaInfo.GUI", "K-Lite.K-LiteCodecPack", "Kodi.Kodi",
    "iTunes.iTunes", "Plex.Plex", "Jellyfin.JellyfinMediaPlayer", "Calibre.Calibre",
    "Amazon.Kindle", "Foxit.FoxitReader", "Anyburn.Anyburn", "BurnAware.BurnAware",
    "Rufus.Rufus", "balenaEtcher.balenaEtcher", "Ventoy.Ventoy", "WinRAR.WinRAR",
    "PeaZip.PeaZip", "Bandizip.Bandizip", "Everything.Everything", "TreeSize.Free", "WizTree.WizTree",
    "ProcessExplorer", "Sysinternals.ProcessExplorer", "NirSoft.NirCmd", "NirSoft.DriverView",
    "Postman.Postman", "Insomnia.Insomnia", "JetBrains.PyCharm.Community", "JetBrains.IntelliJIDEA.Community",
    "Microsoft.WindowsTerminal", "Alacritty.Alacritty", "Microsoft.PowerShell", "Anaconda.Miniconda3",
    "RProject.R", "RStudio.RStudio.OpenSource", "Tableau.Desktop", "DBeaver.DBeaver",
    "PostgreSQL.PostgreSQL", "MongoDB.Server", "Redis.Redis", "MySQL.MySQL", "MariaDB.Server"
)

Write-Host "Analyzing tags from $($packages.Count) packages..." -ForegroundColor Cyan
Write-Host ""

$tagMap = @{}
$packageTags = @{}
$errorCount = 0

foreach ($pkg in $packages) {
    Write-Progress -Activity "Analyzing packages" -Status $pkg -PercentComplete (($packages.IndexOf($pkg) / $packages.Count) * 100)
    
    try {
        $output = winget show $pkg 2>&1 | Out-String
        
        if ($output -match "Tags:\s*\n((?:\s+.+\n)+)") {
            $tagsSection = $matches[1]
            $tags = $tagsSection -split '\n' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }
            
            $packageTags[$pkg] = $tags
            
            foreach ($tag in $tags) {
                if (-not $tagMap.ContainsKey($tag)) {
                    $tagMap[$tag] = @()
                }
                $tagMap[$tag] += $pkg
            }
        }
    } catch {
        $errorCount++
    }
    
    Start-Sleep -Milliseconds 100
}

Write-Progress -Activity "Analyzing packages" -Completed

Write-Host ""
Write-Host "=== TAG ANALYSIS RESULTS ===" -ForegroundColor Green
Write-Host ""
Write-Host "Total packages analyzed: $($packages.Count)" -ForegroundColor Yellow
Write-Host "Packages with tags: $($packageTags.Count)" -ForegroundColor Yellow
Write-Host "Unique tags found: $($tagMap.Count)" -ForegroundColor Yellow
Write-Host "Errors: $errorCount" -ForegroundColor Yellow
Write-Host ""

# Sort tags by popularity (most common first)
$sortedTags = $tagMap.GetEnumerator() | Sort-Object { $_.Value.Count } -Descending

Write-Host "=== TOP 30 MOST COMMON TAGS ===" -ForegroundColor Cyan
Write-Host ""
$sortedTags | Select-Object -First 30 | ForEach-Object {
    $count = $_.Value.Count
    Write-Host ("{0,-30} : {1,3} packages" -f $_.Key, $count)
}

Write-Host ""
Write-Host ""
Write-Host "=== TAG CATEGORIES ===" -ForegroundColor Cyan
Write-Host ""

# Video related
Write-Host "VIDEO/MEDIA TAGS:" -ForegroundColor Magenta
$videoTags = $sortedTags | Where-Object { $_.Key -match "video|media|player|audio|multimedia|streaming" }
foreach ($tag in $videoTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host "DEVELOPMENT TAGS:" -ForegroundColor Magenta
$devTags = $sortedTags | Where-Object { $_.Key -match "develop|code|editor|ide|programming|git|version" }
foreach ($tag in $devTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host "UTILITY/SYSTEM TAGS:" -ForegroundColor Magenta
$utilityTags = $sortedTags | Where-Object { $_.Key -match "utility|utilities|system|tool|disk|cleanup|monitor" }
foreach ($tag in $utilityTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host "ARCHIVE/COMPRESSION TAGS:" -ForegroundColor Magenta
$archiveTags = $sortedTags | Where-Object { $_.Key -match "archive|compress|zip|extract|unzip" }
foreach ($tag in $archiveTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host "BROWSER/WEB TAGS:" -ForegroundColor Magenta
$browserTags = $sortedTags | Where-Object { $_.Key -match "browser|web|internet|chromium" }
foreach ($tag in $browserTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host "OFFICE/PRODUCTIVITY TAGS:" -ForegroundColor Magenta
$officeTags = $sortedTags | Where-Object { $_.Key -match "office|pdf|reader|document|spreadsheet|presentation" }
foreach ($tag in $officeTags) {
    Write-Host "  $($tag.Key) ($($tag.Value.Count)): $($tag.Value -join ', ')"
}

Write-Host ""
Write-Host ""
Write-Host "=== APPS WITH MULTIPLE RELEVANT CATEGORIES ===" -ForegroundColor Cyan
Write-Host ""

foreach ($pkg in $packageTags.Keys) {
    $tags = $packageTags[$pkg]
    $categories = @()
    
    if ($tags -match "video|media|player|audio") { $categories += "Media" }
    if ($tags -match "develop|code|editor|ide") { $categories += "Development" }
    if ($tags -match "utility|system|tool") { $categories += "Utility" }
    if ($tags -match "archive|compress|zip") { $categories += "Archive" }
    if ($tags -match "browser|web|internet") { $categories += "Browser" }
    if ($tags -match "office|pdf|reader|document") { $categories += "Office" }
    if ($tags -match "game|gaming|steam") { $categories += "Gaming" }
    if ($tags -match "security|vpn|password") { $categories += "Security" }
    
    if ($categories.Count -gt 1) {
        Write-Host "$pkg" -ForegroundColor Yellow
        Write-Host "  Categories: $($categories -join ', ')" -ForegroundColor Gray
        Write-Host "  Tags: $($tags -join ', ')" -ForegroundColor DarkGray
        Write-Host ""
    }
}

# Export results to JSON for further analysis
$results = @{
    TotalPackages = $packages.Count
    PackagesWithTags = $packageTags.Count
    UniqueTags = $tagMap.Count
    TagMap = $tagMap
    PackageTags = $packageTags
}

$results | ConvertTo-Json -Depth 10 | Out-File "tag_analysis_results.json"
Write-Host "Results exported to: tag_analysis_results.json" -ForegroundColor Green
