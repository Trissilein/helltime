[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$nativeRoot = Join-Path $repoRoot 'native'
$sourceRoot = Join-Path $nativeRoot 'CMakeLists.txt'
$buildRoot = Join-Path $nativeRoot 'build-release-vs18'
$distRoot = Join-Path $repoRoot 'dist'
$tauriBinary = Join-Path $distRoot 'helltime.exe'
$nativeBinary = Join-Path $buildRoot 'bin\Release\helltime.exe'
$releaseBinary = Join-Path $distRoot 'helltime-native.exe'
$generator = 'Visual Studio 18 2026'
$maxReleaseBytes = 6 * 1024 * 1024

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)] [string]$FilePath,
        [Parameter(Mandatory = $true)] [string[]]$ArgumentList,
        [Parameter(Mandatory = $true)] [string]$Description
    )

    Write-Host "==> $Description"
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Leaf)) {
    throw "Native CMake source is missing: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $tauriBinary -PathType Leaf)) {
    throw "Existing Tauri artifact is missing; refusing to overwrite without hash evidence: $tauriBinary"
}

$tauriHashBefore = (Get-FileHash -LiteralPath $tauriBinary -Algorithm SHA256).Hash

$cmakeHelp = (& cmake --help 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to query CMake generators.'
}
if ($cmakeHelp -notmatch [regex]::Escape($generator)) {
    throw "Required CMake generator is unavailable: $generator"
}

Invoke-NativeCommand -FilePath 'cmake' -ArgumentList @(
    '-S', $nativeRoot,
    '-B', $buildRoot,
    '-G', $generator,
    '-A', 'x64'
) -Description 'Configure native Release build (VS18 x64)'

Invoke-NativeCommand -FilePath 'cmake' -ArgumentList @(
    '--build', $buildRoot,
    '--config', 'Release',
    '--parallel'
) -Description 'Build native Release binary'

if (-not (Test-Path -LiteralPath $nativeBinary -PathType Leaf)) {
    throw "Native build output is missing: $nativeBinary"
}

Invoke-NativeCommand -FilePath 'ctest' -ArgumentList @(
    '--test-dir', $buildRoot,
    '-C', 'Release',
    '--output-on-failure'
) -Description 'Run native CTest suite'

$nativeInfo = Get-Item -LiteralPath $nativeBinary
if ($nativeInfo.Length -gt $maxReleaseBytes) {
    throw "Native release binary exceeds 6 MiB: $($nativeInfo.Length) bytes"
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
Copy-Item -LiteralPath $nativeBinary -Destination $releaseBinary -Force

$tauriHashAfter = (Get-FileHash -LiteralPath $tauriBinary -Algorithm SHA256).Hash
if ($tauriHashBefore -cne $tauriHashAfter) {
    throw "Tauri artifact changed unexpectedly: before=$tauriHashBefore after=$tauriHashAfter"
}

$releaseHash = (Get-FileHash -LiteralPath $releaseBinary -Algorithm SHA256).Hash
$releaseInfo = Get-Item -LiteralPath $releaseBinary

Write-Host ''
Write-Host 'Release evidence:'
Write-Host "  Native source: $nativeBinary"
Write-Host "  Release artifact: $releaseBinary"
Write-Host "  Release size: $($releaseInfo.Length) bytes"
Write-Host "  Release SHA-256: $releaseHash"
Write-Host "  Tauri SHA-256 before: $tauriHashBefore"
Write-Host "  Tauri SHA-256 after:  $tauriHashAfter"
Write-Host '  Tauri artifact unchanged: PASS'
Write-Host '  PDB copied to dist: NO'
Write-Host "  <= 6 MiB: $($(if ($releaseInfo.Length -le $maxReleaseBytes) { 'PASS' } else { 'FAIL' }))"
