#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$BuildRoot,
    [Parameter(Mandatory = $true, Position = 1)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (![IO.Path]::IsPathRooted($BuildRoot)) {
    $BuildRoot = Join-Path (Get-Location) $BuildRoot
}
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)

$MetadataPath = Join-Path $SourceRoot 'packages\win_installer\assdraw.json'
$Metadata = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json

foreach ($Field in @('repository', 'branch')) {
    if ([string]::IsNullOrWhiteSpace([string]$Metadata.$Field)) {
        throw "ASSDraw source metadata '$Field' is not configured in $MetadataPath."
    }
}

if ($Metadata.repository -cne 'https://github.com/amanosatosi/assdraw') {
    throw "Unexpected ASSDraw repository '$($Metadata.repository)'."
}

if ($Metadata.branch -cne 'master') {
    throw "Unexpected ASSDraw branch '$($Metadata.branch)'."
}

$AssDrawSource = Join-Path $BuildRoot 'assdraw-src'
$AssDrawBuild = Join-Path $BuildRoot 'assdraw-build'
$DestinationDir = Join-Path $BuildRoot 'installer-deps\assdraw'
$Destination = Join-Path $DestinationDir 'ASSDraw3.exe'

Write-Host 'Building bundled ASSDraw from source'
Write-Host "Repository: $($Metadata.repository)"
Write-Host "Branch:     $($Metadata.branch)"
Write-Host "Source:     $AssDrawSource"
Write-Host "Build:      $AssDrawBuild"
Write-Host "Stage:      $Destination"

# Resolve the configured branch exactly once. The source, CMake, and staging
# directories are recreated so a stale checkout/build can never select or
# package an older ASSDraw revision.
if (Test-Path -LiteralPath $AssDrawSource) {
    Remove-Item -LiteralPath $AssDrawSource -Recurse -Force
}
if (Test-Path -LiteralPath $AssDrawBuild) {
    Remove-Item -LiteralPath $AssDrawBuild -Recurse -Force
}
if (Test-Path -LiteralPath $DestinationDir) {
    Remove-Item -LiteralPath $DestinationDir -Recurse -Force
}
New-Item -ItemType Directory -Path $AssDrawSource -Force | Out-Null

& git -C $AssDrawSource init
if ($LASTEXITCODE -ne 0) {
    throw 'git init failed for ASSDraw source checkout.'
}

& git -C $AssDrawSource remote add origin $Metadata.repository
if ($LASTEXITCODE -ne 0) {
    throw 'Could not configure the ASSDraw Git remote.'
}

& git -C $AssDrawSource fetch --no-tags --depth=1 origin $Metadata.branch
if ($LASTEXITCODE -ne 0) {
    throw "Could not fetch ASSDraw branch $($Metadata.branch)."
}

$ResolvedCommit = (& git -C $AssDrawSource rev-parse --verify FETCH_HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'Could not resolve ASSDraw FETCH_HEAD to an exact commit.'
}

& git -C $AssDrawSource checkout --detach FETCH_HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Could not check out resolved ASSDraw commit $ResolvedCommit."
}

$CheckedOutCommit = (& git -C $AssDrawSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the checked-out ASSDraw commit.'
}
if ($CheckedOutCommit -cne $ResolvedCommit) {
    throw "ASSDraw checkout mismatch. Resolved $ResolvedCommit, checked out $CheckedOutCommit."
}

Write-Host "Resolved commit: $ResolvedCommit"

New-Item -ItemType Directory -Path $AssDrawBuild -Force | Out-Null

# ASSDraw owns its dependency graph. Its CMake project pins and builds the
# required wxWidgets and AGG sources statically and forces the static MSVC
# runtime. Aegisub should not duplicate that dependency knowledge.
& cmake `
    -S $AssDrawSource `
    -B $AssDrawBuild `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) {
    throw 'ASSDraw CMake configuration failed.'
}

& cmake --build $AssDrawBuild --config Release --parallel 2 --verbose
if ($LASTEXITCODE -ne 0) {
    throw 'ASSDraw Release build failed.'
}

& ctest --test-dir $AssDrawBuild -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw 'ASSDraw non-interactive tests failed.'
}

$BuiltExe = Join-Path $AssDrawBuild 'Release\ASSDraw3.exe'
if (!(Test-Path -LiteralPath $BuiltExe -PathType Leaf)) {
    throw "ASSDraw build succeeded but the expected executable is missing: $BuiltExe"
}

# Reuse ASSDraw's own CI audits. This catches accidental wx/AGG/CRT DLL
# dependencies as well as missing embedded resources before Aegisub packages
# the executable.
$VerifyScript = Join-Path $AssDrawSource 'ci\verify-windows-binary.ps1'
if (!(Test-Path -LiteralPath $VerifyScript -PathType Leaf)) {
    throw "ASSDraw binary verification script is missing: $VerifyScript"
}
& $VerifyScript -Executable $BuiltExe
if ($LASTEXITCODE -ne 0) {
    throw 'ASSDraw static dependency/resource verification failed.'
}

$SmokeScript = Join-Path $AssDrawSource 'ci\smoke-test-windows.ps1'
if (!(Test-Path -LiteralPath $SmokeScript -PathType Leaf)) {
    throw "ASSDraw standalone smoke-test script is missing: $SmokeScript"
}
& $SmokeScript -Executable $BuiltExe
if ($LASTEXITCODE -ne 0) {
    throw 'ASSDraw standalone smoke test failed.'
}

# The package payload is deliberately one file. Do not copy the CMake build
# tree, wxWidgets libraries, AGG libraries, CHM files, or runtime DLLs.
New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
Copy-Item -LiteralPath $BuiltExe -Destination $Destination

$Payload = @(Get-ChildItem -LiteralPath $DestinationDir -File)
if ($Payload.Count -ne 1 -or $Payload[0].Name -cne 'ASSDraw3.exe') {
    throw 'ASSDraw staging must contain ASSDraw3.exe and no companion runtime files.'
}

$Stream = [IO.File]::OpenRead($Destination)
try {
    if ($Stream.ReadByte() -ne 0x4d -or $Stream.ReadByte() -ne 0x5a) {
        throw "$Destination is not a Windows executable (missing MZ header)."
    }
}
finally {
    $Stream.Dispose()
}

$Hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host ''
Write-Host 'ASSDraw built and staged successfully.'
Write-Host "Repository:      $($Metadata.repository)"
Write-Host "Branch:          $($Metadata.branch)"
Write-Host "Resolved commit: $CheckedOutCommit"
Write-Host "Staged EXE:    $Destination"
Write-Host "SHA256:        $Hash"

if ($env:GITHUB_OUTPUT) {
    "resolved_commit=$CheckedOutCommit" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "sha256=$Hash" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}

if ($env:GITHUB_STEP_SUMMARY) {
    @"
### Bundled ASSDraw
- Repository: ``$($Metadata.repository)``
- Branch: ``$($Metadata.branch)``
- Resolved commit: ``$CheckedOutCommit``
- Payload: ``ASSDraw3.exe`` only
- SHA256: ``$Hash``
- ASSDraw tests: passed
- ASSDraw binary audit: passed
- ASSDraw standalone smoke test: passed
"@ | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}
