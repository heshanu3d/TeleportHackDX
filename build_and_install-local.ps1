param(
    [string]$ToolchainRoot = $env:LLVM_MINGW_ROOT,
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [string]$DestinationPath = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
$package = @(& (Join-Path $PSScriptRoot 'build-local.ps1') -ToolchainRoot $ToolchainRoot -Jobs $Jobs -PassThru |
    ForEach-Object {
        if ($_ -is [pscustomobject] -and $_.PSObject.Properties['TeleportHackDXPackagePath']) {
            $_.TeleportHackDXPackagePath
        }
        else { Write-Host $_ }
    })
if ($package.Count -ne 1) { throw 'Build did not return exactly one release package; nothing installed.' }
& (Join-Path $PSScriptRoot 'install-local.ps1') -PackagePath $package[0] -DestinationPath $DestinationPath
