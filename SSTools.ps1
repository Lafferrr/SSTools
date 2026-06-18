if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Please run this script as Administrator." -ForegroundColor Red
    exit
}

$DestinationFolder = "C:\SSTools"
$ZipUrl            = "https://github.com/Lafferrr/SSTools/archive/refs/heads/main.zip"
$ZipPath           = Join-Path $DestinationFolder "SSTools.zip"

New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null

Write-Host "Downloading SSTools..." -ForegroundColor Cyan

$wc = New-Object System.Net.WebClient
$wc.DownloadFile($ZipUrl, $ZipPath)
$wc.Dispose()

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)

foreach ($entry in $zip.Entries) {
    if ($entry.FullName -notmatch '^[^/]+/SSTools/') { continue }

    $relative = $entry.FullName -replace '^[^/]+/SSTools/', ''
    if ($relative -eq '') { continue }

    $destPath = Join-Path $DestinationFolder $relative

    if ($entry.FullName.EndsWith('/')) {
        New-Item -ItemType Directory -Path $destPath -Force | Out-Null
    } else {
        $dirPath = Split-Path $destPath -Parent
        if (-not (Test-Path $dirPath)) {
            New-Item -ItemType Directory -Path $dirPath -Force | Out-Null
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destPath, $true)
    }
}

$zip.Dispose()
Remove-Item $ZipPath -Force

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
