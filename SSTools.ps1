$DestinationFolder = "C:\SSToolsTest"
$ApiBase           = "https://api.github.com/repos/Lafferrr/SSTools/contents/SSTools"

function Download-Tree {
    param ([string]$ApiUrl, [string]$LocalBase)

    $entries = Invoke-RestMethod -Uri $ApiUrl -UseBasicParsing

    foreach ($entry in $entries) {
        $dest = Join-Path $LocalBase $entry.name

        if ($entry.type -eq "dir") {
            New-Item -ItemType Directory -Path $dest -Force | Out-Null
            Download-Tree -ApiUrl $entry.url -LocalBase $dest
        } else {
            Invoke-WebRequest -Uri $entry.download_url -OutFile $dest -UseBasicParsing
            Write-Host "Downloaded: $($entry.name)" -ForegroundColor Green
        }
    }
}

New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null
Write-Host "Downloading SSTools to $DestinationFolder..." -ForegroundColor Cyan
Download-Tree -ApiUrl $ApiBase -LocalBase $DestinationFolder

$bamRevealDir = Join-Path $DestinationFolder "BAMRevealer"
New-Item -ItemType Directory -Path $bamRevealDir -Force | Out-Null
Write-Host "Downloading BAMReveal..." -ForegroundColor Cyan
Invoke-WebRequest -Uri "https://github.com/Orbdiff/BAMReveal/releases/download/v1.3.1/BAMReveal.exe" -OutFile (Join-Path $bamRevealDir "BAMReveal.exe") -UseBasicParsing
Write-Host "Downloaded: BAMReveal.exe" -ForegroundColor Green

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
