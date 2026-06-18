$DestinationFolder = "C:\SSToolstest"
$ApiBase           = "https://api.github.com/repos/Lafferrr/SSTools/contents/SSTools"

$reg = "HKLM:\SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths"
New-ItemProperty -Path $reg -Name $DestinationFolder -Value 0 -PropertyType DWORD -Force | Out-Null

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

$files = Get-FileList -ApiUrl $ApiBase -LocalBase $DestinationFolder
$files += [PSCustomObject]@{
    Url  = "https://github.com/Orbdiff/BAMReveal/releases/download/v1.3.1/BAMReveal.exe"
    Dest = Join-Path $DestinationFolder "BAMRevealer\BAMReveal.exe"
}

New-Item -ItemType Directory -Path (Join-Path $DestinationFolder "BAMRevealer") -Force | Out-Null

Write-Host "Downloading $($files.Count) files" -ForegroundColor Cyan

$clients = @()
$tasks   = @()

foreach ($file in $files) {
    $client = New-Object System.Net.WebClient
    $clients += $client
    $tasks   += $client.DownloadFileTaskAsync($file.Url, $file.Dest)
}

foreach ($i in 0..($tasks.Count - 1)) {
    $tasks[$i].GetAwaiter().GetResult()
    Write-Host "Downloaded: $(Split-Path $files[$i].Dest -Leaf)" -ForegroundColor Green
    $clients[$i].Dispose()
}

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
