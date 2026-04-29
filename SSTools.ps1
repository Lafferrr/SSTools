#Requires -RunAsAdministrator

# Force TLS 1.2 for GitHub compatibility
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$GitHubRepo          = "Lafferrr/SSTools"
$Branch              = "main"
$SourceFolderInRepo  = "SSTools"
$DestinationFolder   = "C:\SSTools"
$tempDir             = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString())

New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

$zipUrl  = "https://github.com/$GitHubRepo/archive/refs/heads/$Branch.zip"
$zipPath = Join-Path $tempDir "repo.zip"

Write-Host "Downloading files..." -ForegroundColor Green

try {
    $webClient = New-Object System.Net.WebClient
    $webClient.DownloadFile($zipUrl, $zipPath)
    $webClient.Dispose()
} catch {
    Write-Error "Failed to download repository: $_`nCheck the URL and your internet connection."
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

try {
    Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force
} catch {
    Write-Error "Failed to extract archive: $_"
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

$extractedFolderName  = ($GitHubRepo -split '/')[-1] + "-$Branch"
$extractedPath        = Join-Path $tempDir $extractedFolderName
$sourceSSToolsFolder  = Join-Path $extractedPath $SourceFolderInRepo

if (-not (Test-Path $sourceSSToolsFolder)) {
    Write-Error "Could not find folder '$SourceFolderInRepo' inside the repository."
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}

if (-not (Test-Path $DestinationFolder)) {
    New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null
}

Copy-Item -Path "$sourceSSToolsFolder\*" -Destination $DestinationFolder -Recurse -Force

Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
