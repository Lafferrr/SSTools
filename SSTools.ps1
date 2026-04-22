$GitHubRepo = "Lafferrr/SSTools"
$Branch = "main"
$SourceFolderInRepo = "SSTools"
$DestinationFolder = "C:\SSTools"

$tempDir = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

$zipUrl = "https://github.com/$GitHubRepo/archive/refs/heads/$Branch.zip"
$zipPath = Join-Path $tempDir "repo.zip"

Write-Host "Downloading files..." -ForegroundColor Green

$webClient = New-Object System.Net.WebClient
try {
    $webClient.DownloadFile($zipUrl, $zipPath)
} catch {
    Write-Error "Failed to download repository. Check the URL and your internet connection."
    $webClient.Dispose()
    exit 1
}
$webClient.Dispose()

Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force

$extractedFolderName = ($GitHubRepo -split '/')[-1] + "-$Branch"
$extractedPath = Join-Path $tempDir $extractedFolderName

$sourceSSToolsFolder = Join-Path $extractedPath $SourceFolderInRepo
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
