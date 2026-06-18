if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Please run this script as Administrator." -ForegroundColor Red
    exit
}

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

$files = Get-FileList -ApiUrl $ApiBase -LocalBase $DestinationFolder

Write-Host "Downloading tools into $DestinationFolder..." -ForegroundColor Cyan

$clients = @()
$tasks   = @()

foreach ($file in $files) {
    $client = New-Object System.Net.WebClient
    $clients += $client
    $tasks   += $client.DownloadFileTaskAsync($file.Url, $file.Dest)
}

foreach ($i in 0..($tasks.Count - 1)) {
    $tasks[$i].GetAwaiter().GetResult()
    $clients[$i].Dispose()
}

Write-Host "Completed! Tools now in $DestinationFolder" -ForegroundColor Green
