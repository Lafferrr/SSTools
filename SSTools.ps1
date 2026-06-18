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
[System.IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $DestinationFolder)

Remove-Item $ZipPath -Force

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
