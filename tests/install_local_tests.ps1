$ErrorActionPreference = 'Stop'
$project = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture = Join-Path $project ('build\install-tests\' + [guid]::NewGuid().ToString('N'))
$package = Join-Path $fixture 'package'
$destination = Join-Path $fixture 'dist'
$broken = Join-Path $fixture 'incomplete'
$installer = Join-Path $project 'install-local.ps1'
$guide = (-join @([char]0x4f7f, [char]0x7528, [char]0x8bf4, [char]0x660e)) + '.html'
New-Item -ItemType Directory -Force -Path $package, (Join-Path $package 'server'), $broken | Out-Null
$files = @('TeleportHackDX.dll', 'TeleportHackDesktop.exe', 'TeleportHackLauncher.exe',
           'TeleportHackCApi.dll', $guide, 'favlist.fav', 'hotkey.txt',
           'server\TeleportManager.lua', 'server\README.md')
foreach ($name in $files) { [System.IO.File]::WriteAllText((Join-Path $package $name), "old:$name") }

& $installer -PackagePath $package -DestinationPath $destination
foreach ($name in $files) {
    if ((Get-FileHash -LiteralPath (Join-Path $destination $name)).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $package $name)).Hash) { throw "First install mismatch: $name" }
}
foreach ($name in @('favlist.fav', 'hotkey.txt', 'settings.json', 'custom.fav')) {
    [System.IO.File]::WriteAllText((Join-Path $destination $name), "user:$name")
}
[System.IO.File]::WriteAllText((Join-Path $destination 'server\TeleportManager.lua'), 'server-custom')
[System.IO.File]::WriteAllText((Join-Path $package 'server\TeleportManager.lua'), 'server-new')
[System.IO.File]::WriteAllText((Join-Path $package 'TeleportHackDX.dll'), 'binary-new')
& $installer -PackagePath $package -DestinationPath $destination
foreach ($name in @('favlist.fav', 'hotkey.txt', 'settings.json', 'custom.fav')) {
    if ([System.IO.File]::ReadAllText((Join-Path $destination $name)) -ne "user:$name") {
        throw "User file overwritten: $name"
    }
}
if ([System.IO.File]::ReadAllText((Join-Path $destination 'TeleportHackDX.dll')) -ne 'binary-new') {
    throw 'Program binary was not updated.'
}
if ([System.IO.File]::ReadAllText((Join-Path $destination 'server\TeleportManager.lua')) -ne 'server-new') {
    throw 'Server integration was not updated.'
}
$backups = @(Get-ChildItem -LiteralPath (Join-Path $destination 'server') -Filter 'TeleportManager.lua.*.bak')
if ($backups.Count -ne 1 -or [System.IO.File]::ReadAllText($backups[0].FullName) -ne 'server-custom') {
    throw 'Previous server integration was not backed up.'
}
$failed = $false
try { & $installer -PackagePath $broken -DestinationPath $destination }
catch { $failed = $true }
if (-not $failed -or [System.IO.File]::ReadAllText((Join-Path $destination 'TeleportHackDX.dll')) -ne 'binary-new') {
    throw 'Incomplete package was not rejected before installation.'
}
$failed = $false
try { & $installer -PackagePath $package -DestinationPath (Join-Path $package 'nested') }
catch { $failed = $true }
if (-not $failed) { throw 'Overlapping package and installation were not rejected.' }
Write-Host 'Install-local tests passed.'
