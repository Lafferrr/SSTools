if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Please run this script as Administrator." -ForegroundColor Red
    exit
}

$DestinationFolder = "C:\SSTools"
$ZipUrl            = "https://github.com/Lafferrr/SSTools/archive/refs/heads/main.zip"
$TempZip           = Join-Path $env:TEMP "SSTools.zip"
$TempExtract       = Join-Path $env:TEMP "SSTools_extracted"

Add-MpPreference -ExclusionPath $DestinationFolder

Write-Host "Downloading tools into $DestinationFolder..." -ForegroundColor Cyan

$client = New-Object System.Net.WebClient
$client.DownloadFile($ZipUrl, $TempZip)
$client.Dispose()

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($TempZip, $TempExtract)

$source = Join-Path $TempExtract "SSTools-main\SSTools"
New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null
Copy-Item -Path "$source\*" -Destination $DestinationFolder -Recurse -Force

Remove-Item $TempZip     -Force
Remove-Item $TempExtract -Recurse -Force

$bamRevealDir = Join-Path $DestinationFolder "BAMRevealer"
New-Item -ItemType Directory -Path $bamRevealDir -Force | Out-Null

$client = New-Object System.Net.WebClient
$client.DownloadFile("https://github.com/Orbdiff/BAMReveal/releases/download/v1.3.1/BAMReveal.exe", (Join-Path $bamRevealDir "BAMReveal.exe"))
$client.Dispose()

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
