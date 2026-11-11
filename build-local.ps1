param(
    [string]$ToolchainRoot = $env:LLVM_MINGW_ROOT,
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [switch]$PassThru
)

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
if (-not $ToolchainRoot) {
    $sdkRoot = 'D:\SDK\llvm-mingw'
    if (Test-Path -LiteralPath $sdkRoot) {
        $candidate = Get-ChildItem -LiteralPath $sdkRoot -Directory |
            Sort-Object Name -Descending |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\i686-w64-mingw32-clang++.exe') } |
            Select-Object -First 1
        if ($candidate) { $ToolchainRoot = $candidate.FullName }
    }
}
if (-not $ToolchainRoot) { throw 'LLVM-MinGW not found. Set LLVM_MINGW_ROOT or pass -ToolchainRoot.' }
$toolBin = Join-Path $ToolchainRoot 'bin'
$cCompiler = Join-Path $toolBin 'i686-w64-mingw32-clang.exe'
$cppCompiler = Join-Path $toolBin 'i686-w64-mingw32-clang++.exe'
foreach ($file in @($cCompiler, $cppCompiler)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Compiler not found: $file" }
}
foreach ($command in @('cmake', 'ctest', 'ninja')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "$command not found in PATH" }
}

$build = Join-Path $project 'build\local-mingw'
$originalPath = $env:PATH
try {
    $env:PATH = "$toolBin;$originalPath"
    $cmakeArgs = @('-S', $project, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$cCompiler", "-DCMAKE_CXX_COMPILER=$cppCompiler",
        '-DCMAKE_CXX_FLAGS=-fms-extensions', '-DCMAKE_SHARED_LINKER_FLAGS=-static',
        '-DCMAKE_EXE_LINKER_FLAGS=-static')
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    & cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }
    & ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }

    # A fresh folder each time prevents overwriting a user's edited data files.
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $package = Join-Path $build ("release-$stamp-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $package | Out-Null
    foreach ($name in @('TeleportHackDX.dll', 'TeleportHackDesktop.exe', 'TeleportHackLauncher.exe', 'TeleportHackCApi.dll')) {
        Copy-Item -LiteralPath (Join-Path $build "bin\$name") -Destination $package
    }
    $guideName = (-join @([char]0x4f7f, [char]0x7528, [char]0x8bf4, [char]0x660e)) + '.html'
    foreach ($name in @('favlist.fav', 'hotkey.txt', $guideName)) {
        Copy-Item -LiteralPath (Join-Path $project $name) -Destination $package
    }
    $server = Join-Path $package 'server'
    New-Item -ItemType Directory -Path $server | Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'integration\vmangos\TeleportManager.lua') -Destination $server
    Copy-Item -LiteralPath (Join-Path $project 'integration\vmangos\README.md') -Destination $server
    Write-Host "Local release: $package"
    $hash = (Get-FileHash -LiteralPath (Join-Path $package 'TeleportHackDX.dll') -Algorithm SHA256).Hash
    Write-Host "TeleportHackDX.dll SHA256: $hash"
    if ($PassThru) { [pscustomobject]@{ TeleportHackDXPackagePath = $package } }
}
finally {
    $env:PATH = $originalPath
}
