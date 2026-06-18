$DestinationFolder = "C:\SSTools"
$ApiBase           = "https://api.github.com/repos/Lafferrr/SSTools/contents/SSTools"

function Get-FileList {
    param ([string]$ApiUrl, [string]$LocalBase)

    $entries = Invoke-RestMethod -Uri $ApiUrl -UseBasicParsing
    $files   = @()

    foreach ($entry in $entries) {
        $dest = Join-Path $LocalBase $entry.name

        if ($entry.type -eq "dir") {
            New-Item -ItemType Directory -Path $dest -Force | Out-Null
            $files += Get-FileList -ApiUrl $entry.url -LocalBase $dest
        } else {
            $files += [PSCustomObject]@{ Url = $entry.download_url; Dest = $dest }
        }
    }

    return $files
}

New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null
Write-Host "Fetching file list..." -ForegroundColor Cyan

$files = Get-FileList -ApiUrl $ApiBase -LocalBase $DestinationFolder
$files += [PSCustomObject]@{
    Url  = "https://github.com/Orbdiff/BAMReveal/releases/download/v1.3.1/BAMReveal.exe"
    Dest = Join-Path $DestinationFolder "BAMRevealer\BAMReveal.exe"
}

New-Item -ItemType Directory -Path (Join-Path $DestinationFolder "BAMRevealer") -Force | Out-Null

Write-Host "Downloading $($files.Count) files in parallel..." -ForegroundColor Cyan

$jobs = @()
foreach ($file in $files) {
    $jobs += Start-Job -ScriptBlock {
        Invoke-WebRequest -Uri $using:file.Url -OutFile $using:file.Dest -UseBasicParsing
        Write-Output "Downloaded: $(Split-Path $using:file.Dest -Leaf)"
    } -ArgumentList $file
}

$jobs | Wait-Job | Receive-Job | ForEach-Object { Write-Host $_ -ForegroundColor Green }
$jobs | Remove-Job

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
