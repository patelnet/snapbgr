<#
.SYNOPSIS
  One-shot build script for SnapBGR (core library + console test).

.DESCRIPTION
  1. Locates or bootstraps vcpkg (clones the pinned 2026.06.24 release if
     VCPKG_ROOT is not set and .\vcpkg does not exist).
  2. Updates the vcpkg.json builtin-baseline to match the checked-out vcpkg.
  3. Configures CMake with the vcpkg toolchain (manifest mode installs the
     pinned onnxruntime/opencv/nlohmann-json automatically).
  4. Builds Release and runs the console smoke test.

.PARAMETER Configuration
  Build configuration (Debug or Release). Default: Release.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Configuration Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$vcpkgTag = '2026.06.24'   # pinned vcpkg release; change here to update

# --- 1. Locate or bootstrap vcpkg -------------------------------------------
if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    $vcpkgRoot = $env:VCPKG_ROOT
    Write-Host "Using existing vcpkg at $vcpkgRoot"
} else {
    $vcpkgRoot = Join-Path $repoRoot 'vcpkg'
    if (-not (Test-Path $vcpkgRoot)) {
        Write-Host "Cloning vcpkg ($vcpkgTag)..."
        # Full clone required: vcpkg versioning checks out pinned port
        # versions from git history, which shallow clones cannot provide.
        git clone --branch $vcpkgTag https://github.com/microsoft/vcpkg.git $vcpkgRoot
        if (-not $?) { throw "Failed to clone vcpkg" }
    }
    if (-not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
        Write-Host 'Bootstrapping vcpkg...'
        & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if (-not $?) { throw "vcpkg bootstrap failed" }
    }
    $env:VCPKG_ROOT = $vcpkgRoot
}

# --- 2. Sync manifest baseline to this vcpkg checkout -----------------------
# vcpkg.json ships with a zeroed baseline placeholder; this pins it to the
# commit of the vcpkg checkout being used so version overrides resolve.
Push-Location $repoRoot
try {
    & (Join-Path $vcpkgRoot 'vcpkg.exe') x-update-baseline
    if (-not $?) { throw "vcpkg x-update-baseline failed" }
} finally {
    Pop-Location
}

# --- 3. Configure ------------------------------------------------------------
$buildDir = Join-Path $repoRoot 'build'
cmake -S $repoRoot -B $buildDir `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
if (-not $?) { throw 'CMake configure failed' }

# --- 4. Build + test ----------------------------------------------------------
cmake --build $buildDir --config $Configuration
if (-not $?) { throw 'Build failed' }

Write-Host 'Running console smoke test (synthetic fallback)...'
ctest --test-dir $buildDir -C $Configuration --output-on-failure
if (-not $?) { throw 'Console test failed' }

Write-Host "Done. Binaries in $buildDir\$Configuration"
