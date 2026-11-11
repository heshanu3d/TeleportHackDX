param(
    [string]$PackagePath,
    [string]$DestinationPath = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot 'build\local-mingw'
if (-not $PackagePath) {
    if (-not (Test-Path -LiteralPath $build -PathType Container)) {
        throw 'No local release found. Run build-local.ps1 first.'
    }
    $latest = Get-ChildItem -LiteralPath $build -Directory -Filter 'release-*' |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No local release found. Run build-local.ps1 first.' }
    $PackagePath = $latest.FullName
}
$source = (Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop).ProviderPath
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw 'PackagePath must be a directory.' }
$destination = [System.IO.Path]::GetFullPath($DestinationPath).TrimEnd('\', '/')
$sourceFull = [System.IO.Path]::GetFullPath($source).TrimEnd('\', '/')
if ($sourceFull.Equals($destination, [System.StringComparison]::OrdinalIgnoreCase) -or
    $sourceFull.StartsWith($destination + '\', [System.StringComparison]::OrdinalIgnoreCase) -or
    $destination.StartsWith($sourceFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package and destination directories must not overlap.'
}

$guide = (-join @([char]0x4f7f, [char]0x7528, [char]0x8bf4, [char]0x660e)) + '.html'
$programFiles = @('TeleportHackDX.dll', 'TeleportHackDesktop.exe', 'TeleportHackLauncher.exe',
                  'TeleportHackCApi.dll', $guide, 'server\TeleportManager.lua', 'server\README.md')
$userFiles = @('favlist.fav', 'hotkey.txt')
foreach ($name in ($programFiles + $userFiles)) {
    $item = Join-Path $sourceFull $name
    if (-not (Test-Path -LiteralPath $item -PathType Leaf)) { throw "Incomplete release: $item" }
}

New-Item -ItemType Directory -Force -Path $destination | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'server') | Out-Null
foreach ($name in $programFiles) {
    $from = Join-Path $sourceFull $name
    $to = Join-Path $destination $name
    if ((Test-Path -LiteralPath $to -PathType Leaf) -and
        (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash) { continue }
    $temporary = Join-Path (Split-Path -Parent $to) ('.teleport-install-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [System.IO.File]::Copy($from, $temporary, $false)
        if (Test-Path -LiteralPath $to -PathType Leaf) {
            if ($name -eq 'server\TeleportManager.lua') {
                $backup = $to + '.' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.bak'
            }
            else {
                $backupDirectory = Join-Path $destination '.install-backups'
                New-Item -ItemType Directory -Force -Path $backupDirectory | Out-Null
                $backup = Join-Path $backupDirectory ((Split-Path -Leaf $to) + '.' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.bak')
            }
            [System.IO.File]::Replace($temporary, $to, $backup)
            Write-Host "Previous file backed up: $backup"
        }
        else { [System.IO.File]::Move($temporary, $to) }
    }
    finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
}
foreach ($name in $userFiles) {
    $to = Join-Path $destination $name
    if (Test-Path -LiteralPath $to) { Write-Host "Preserved existing data: $to"; continue }
    [System.IO.File]::Copy((Join-Path $sourceFull $name), $to, $false)
}
Write-Host "Installed from: $sourceFull"
Write-Host "Installed to: $destination"
Write-Host "TeleportHackDX.dll SHA256: $((Get-FileHash -LiteralPath (Join-Path $destination 'TeleportHackDX.dll') -Algorithm SHA256).Hash)"
