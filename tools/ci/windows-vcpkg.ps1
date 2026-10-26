param (
    [ValidateSet('prepare', 'install')]
    [string]$Mode,
    [ValidateSet('readwrite', 'read')]
    [string]$Access = 'readwrite'
)

$ErrorActionPreference = 'Stop'
$Triplet = 'x64-windows'
$PackagesFile = Join-Path $PSScriptRoot '../../.github/vcpkg/opencv.txt'

function Hash-Text([string]$Text) {
    $Bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

if ($Mode -eq 'prepare') {
    # Use the runner's bootstrapped vcpkg, not the different VS-integrated copy.
    $VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    if (-not $VcpkgRoot) { $VcpkgRoot = 'C:\vcpkg' }
    $VcpkgRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
    $VcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
    # Hosted images can roll out different registries/tools concurrently.
    # Pin both using the existing checkout, without caching that checkout.
    $PinnedRevision = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../../.github/vcpkg/revision.txt')).Trim()
    if ($PinnedRevision -notmatch '^[0-9a-f]{40}$') { throw 'Invalid pinned vcpkg revision.' }
    & git -C $VcpkgRoot cat-file -e "$PinnedRevision^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        & git -C $VcpkgRoot fetch --no-tags --depth=1 origin $PinnedRevision
        if ($LASTEXITCODE -ne 0) { throw 'Could not fetch pinned vcpkg revision.' }
    }
    & git -C $VcpkgRoot checkout --detach $PinnedRevision
    if ($LASTEXITCODE -ne 0) { throw 'Could not select pinned vcpkg revision.' }
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'Could not bootstrap the matching vcpkg tool.' }
    $Baseline = (& git -C $VcpkgRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $Baseline -cne $PinnedRevision) {
        throw 'Could not verify the pinned vcpkg registry baseline.'
    }
    if ($env:VCPKG_OVERLAY_PORTS -or $env:VCPKG_OVERLAY_TRIPLETS) {
        throw 'Add overlay inputs to the binary cache fingerprint before enabling overlays.'
    }
    $Compiler = (Get-Command cl.exe -ErrorAction Stop).Source
    $Toolchain = Hash-Text ((Get-FileHash -LiteralPath $Compiler -Algorithm SHA256).Hash +
        "|$env:VCToolsVersion|$env:WindowsSDKVersion|$env:VSCMD_ARG_TGT_ARCH")
    $Configuration = Hash-Text ([IO.File]::ReadAllText($PackagesFile).Replace("`r`n", "`n") +
        (Get-FileHash -LiteralPath (Join-Path $VcpkgRoot "triplets/$Triplet.cmake")).Hash +
        "|$env:VCPKG_FEATURE_FLAGS")
    $VcpkgIdentity = Hash-Text ($Baseline + (Get-FileHash -LiteralPath $VcpkgExe).Hash)
    $Prefix = "toshiban-vcpkg-v1-Windows-X64-$Triplet-$Toolchain-$Configuration-"
    $Archives = Join-Path $env:RUNNER_TEMP 'vcpkg-binary-archives'
    New-Item -ItemType Directory -Path $Archives -Force | Out-Null

    @(
        "root=$VcpkgRoot"
        "archives=$Archives"
        "key=$Prefix$VcpkgIdentity"
        "prefix=$Prefix"
    ) | Out-File -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    @(
        "VCPKG_ROOT=$VcpkgRoot"
        "VCPKG_BINARY_SOURCES=clear;files,$Archives,$Access"
        "SCCACHE_ERROR_LOG=$(Join-Path $env:RUNNER_TEMP 'sccache-server.log')"
    ) | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
    Write-Host "vcpkg baseline: $Baseline; triplet: $Triplet; binary cache access: $Access"
    Write-Host "MSVC: $env:VCToolsVersion; Windows SDK: $env:WindowsSDKVersion"
    & $VcpkgExe version
    if ($LASTEXITCODE -ne 0) { throw 'Could not query hosted vcpkg.' }
    exit
}

$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot -or -not $env:VCPKG_BINARY_SOURCES) {
    throw 'Run the prepare step and restore binary archives before installing OpenCV.'
}
$Packages = @(Get-Content -LiteralPath $PackagesFile | Where-Object { $_.Trim() })
foreach ($Package in $Packages) {
    if (-not $Package.EndsWith(":$Triplet")) { throw "Unexpected package triplet: $Package" }
}
$Timer = [Diagnostics.Stopwatch]::StartNew()
& (Join-Path $VcpkgRoot 'vcpkg.exe') install @Packages "--vcpkg-root=$VcpkgRoot" "--host-triplet=$Triplet"
if ($LASTEXITCODE -ne 0) { throw 'OpenCV installation failed.' }
$Timer.Stop()
Write-Host ('OpenCV/vcpkg installation: {0:N1} seconds' -f $Timer.Elapsed.TotalSeconds)

$OpenCVPrefix = Join-Path $VcpkgRoot "installed/$Triplet"
$OpenCVBin = Join-Path $OpenCVPrefix 'bin'
if (-not (Test-Path -LiteralPath (Join-Path $OpenCVPrefix 'share/opencv4/OpenCVConfig.cmake'))) {
    throw 'vcpkg did not install the expected OpenCV CMake configuration.'
}
$CMakePrefix = $OpenCVPrefix
if ($env:CMAKE_PREFIX_PATH) { $CMakePrefix += ";$env:CMAKE_PREFIX_PATH" }
@(
    "OpenCV_DIR=$OpenCVPrefix\share\opencv4"
    "OPENCV_BIN_DIR=$OpenCVBin"
    "CMAKE_PREFIX_PATH=$CMakePrefix"
) | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
$OpenCVBin | Out-File -LiteralPath $env:GITHUB_PATH -Encoding utf8 -Append

# A fallback restore may contain obsolete ABIs. Retain only installed packages,
# keeping this archive small instead of accumulating previous port versions.
if ($Access -eq 'readwrite') {
    $Status = Get-Content -LiteralPath (Join-Path $VcpkgRoot 'installed/vcpkg/status') -Raw
    $Abis = @([regex]::Matches($Status, '(?m)^Abi: ([0-9a-f]{64})\r?$') | ForEach-Object { $_.Groups[1].Value })
    if (-not $Abis.Count) { throw 'No installed package ABIs found; refusing to save an unbounded cache.' }
    $Archives = [IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP 'vcpkg-binary-archives'))
    $ExpectedParent = [IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\')
    if ((Split-Path -Parent $Archives).TrimEnd('\') -cne $ExpectedParent) {
        throw 'Binary archive directory escaped RUNNER_TEMP.'
    }
    foreach ($Archive in Get-ChildItem -LiteralPath $Archives -Recurse -File -Filter '*.zip') {
        if ($Archive.BaseName -notin $Abis) { Remove-Item -LiteralPath $Archive.FullName -Force }
    }
    Write-Host "Binary cache contains $((Get-ChildItem -LiteralPath $Archives -Recurse -File -Filter '*.zip').Count) ABI archives."
}
