#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$BuildRoot,
    [Parameter(Mandatory = $true, Position = 1)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$TemporaryRoot = if ($Env:RUNNER_TEMP) { $Env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$DependencyExe = Join-Path $BuildRoot 'installer-deps\assdraw\ASSDraw3.exe'
$PortableDir = Join-Path $BuildRoot 'aegisub-portable'
$PortableExe = Join-Path $PortableDir 'ASSDraw3.exe'
$PortableZip = Join-Path $BuildRoot 'aegisub-portable-64.zip'

if (!(Test-Path -LiteralPath $DependencyExe -PathType Leaf)) {
    throw "Staged ASSDraw executable is missing: $DependencyExe"
}
$ExpectedHash = (Get-FileHash -LiteralPath $DependencyExe -Algorithm SHA256).Hash.ToLowerInvariant()

function Assert-FileHashMatches {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required ASSDraw payload is missing: $Path"
    }
    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Actual -cne $ExpectedHash) {
        throw "ASSDraw payload hash mismatch at $Path. Expected $ExpectedHash, got $Actual."
    }
}

function Assert-WindowsExecutable {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Stream = [IO.File]::OpenRead($Path)
    try {
        if ($Stream.ReadByte() -ne 0x4d -or $Stream.ReadByte() -ne 0x5a) {
            throw "$Path is not a Windows executable (missing MZ header)."
        }
    }
    finally {
        $Stream.Dispose()
    }
}

Assert-FileHashMatches -Path $DependencyExe
Assert-WindowsExecutable -Path $DependencyExe
Assert-FileHashMatches -Path $PortableExe

$DependencyPayload = @(Get-ChildItem -LiteralPath (Split-Path $DependencyExe) -File)
if ($DependencyPayload.Count -ne 1 -or $DependencyPayload[0].Name -cne 'ASSDraw3.exe') {
    throw 'The ASSDraw dependency payload must contain ASSDraw3.exe and no companion DLLs or CHM files.'
}

$ToolSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\command\tool.cpp') -Raw
if ($ToolSource -notmatch 'config::path->Decode\("\?data/ASSDraw3\.exe"\)') {
    throw 'tool/assdraw no longer resolves the packaged ?data/ASSDraw3.exe path.'
}
foreach ($Resource in @('button\assdraw_16.png', 'button_dark\assdraw_16.png')) {
    $Manifest = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\bitmaps\manifest.respack') -Raw
    if ($Manifest -notmatch [regex]::Escape($Resource.Replace('\', '/'))) {
        throw "The existing ASSDraw command icon resource is missing: $Resource"
    }
}
foreach ($UiFile in @('default_menu.json', 'default_toolbar.json')) {
    $UiText = Get-Content -LiteralPath (Join-Path $SourceRoot "src\libresrc\$UiFile") -Raw
    if ([regex]::Matches($UiText, 'tool/assdraw').Count -ne 1) {
        throw "$UiFile must contain exactly one tool/assdraw entry."
    }
}

if (!(Test-Path -LiteralPath $PortableZip -PathType Leaf)) {
    throw "Portable archive is missing: $PortableZip"
}
$ZipList = & 7z l $PortableZip | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect $PortableZip with 7z."
}
if ($ZipList -notmatch '(?im)^.*aegisub-portable\\ASSDraw3\.exe\s*$') {
    throw 'Portable archive is missing ASSDraw3.exe at its application root.'
}
if ($ZipList -match '(?i)ASSDraw3\.chm|tools[\\/]assdraw') {
    throw 'Portable archive contains an unsupported ASSDraw CHM or nested tools/assdraw payload.'
}

$ZipExtractRoot = Join-Path $TemporaryRoot ("assdraw-zip-verify-" + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $ZipExtractRoot -Force | Out-Null
    & 7z e $PortableZip 'aegisub-portable\ASSDraw3.exe' "-o$ZipExtractRoot" -y | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not extract ASSDraw3.exe from the portable archive.'
    }
    Assert-FileHashMatches -Path (Join-Path $ZipExtractRoot 'ASSDraw3.exe')
}
finally {
    Remove-Item -LiteralPath $ZipExtractRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$Dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (!$Dumpbin) {
    $Vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $VsInstall = & $Vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $Dumpbin = Get-ChildItem "$VsInstall\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1
}
if (!$Dumpbin) {
    throw 'dumpbin.exe was not found.'
}
$DumpbinPath = if ($Dumpbin.Path) { $Dumpbin.Path } else { $Dumpbin.FullName }
$DependencyOutput = & $DumpbinPath /DEPENDENTS $DependencyExe | Out-String
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin /DEPENDENTS failed for ASSDraw3.exe.'
}
$Imports = [regex]::Matches($DependencyOutput, '(?im)^\s+([A-Za-z0-9_.+\-]+\.(?:dll|drv))\s*$') |
    ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
    Sort-Object -Unique
if (!$Imports) {
    throw 'No ASSDraw imports were parsed from dumpbin output.'
}
$ForbiddenImports = $Imports | Where-Object {
    $_ -match '^(WX.*\.DLL|AGG.*\.DLL|VCRUNTIME.*\.DLL|MSVCP.*\.DLL|CONCRT.*\.DLL|UCRTBASE\.DLL|LIBGCC.*\.DLL|LIBSTDC\+\+.*\.DLL|LIBWINPTHREAD.*\.DLL)$'
}
if ($ForbiddenImports) {
    throw "ASSDraw has forbidden companion runtime imports: $($ForbiddenImports -join ', ')"
}

$SmokeRoot = Join-Path $TemporaryRoot ("assdraw-package-smoke-" + [guid]::NewGuid().ToString('N'))
$UnicodeDir = Join-Path $SmokeRoot 'Aegisub Toshi-ban 日本語'
$SmokeExe = Join-Path $UnicodeDir 'ASSDraw3.exe'
$AssDrawProcess = $null
try {
    New-Item -ItemType Directory -Path $UnicodeDir -Force | Out-Null
    Copy-Item -LiteralPath $PortableExe -Destination $SmokeExe
    $AssDrawProcess = Start-Process -FilePath $SmokeExe -WorkingDirectory $UnicodeDir -PassThru
    Start-Sleep -Seconds 5
    $AssDrawProcess.Refresh()
    if ($AssDrawProcess.HasExited) {
        throw "ASSDraw3.exe exited during startup with code $($AssDrawProcess.ExitCode)."
    }
    if ($AssDrawProcess.MainWindowHandle -eq 0) {
        throw 'ASSDraw3.exe did not create a visible window when launched from a Unicode path containing spaces.'
    }
}
finally {
    if ($AssDrawProcess -and !$AssDrawProcess.HasExited) {
        Stop-Process -Id $AssDrawProcess.Id -Force
        $AssDrawProcess.WaitForExit()
    }
    Remove-Item -LiteralPath $SmokeRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$Installers = @(Get-ChildItem -LiteralPath $BuildRoot -Filter 'Aegisub-*.exe' -File)
if ($Installers.Count -ne 1) {
    throw "Expected exactly one Aegisub installer in $BuildRoot, found $($Installers.Count)."
}
$InstallSmokeRoot = Join-Path $TemporaryRoot ("aegisub-installer-smoke-" + [guid]::NewGuid().ToString('N'))
$InstallDir = Join-Path $InstallSmokeRoot 'Aegisub Toshi-ban 日本語'
$Uninstaller = $null
try {
    New-Item -ItemType Directory -Path $InstallSmokeRoot -Force | Out-Null
    $QuotedInstallDir = '"' + $InstallDir + '"'
    $InstallProcess = Start-Process -FilePath $Installers[0].FullName -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$QuotedInstallDir"
    ) -Wait -PassThru
    if ($InstallProcess.ExitCode -ne 0) {
        throw "Installer smoke test failed with exit code $($InstallProcess.ExitCode)."
    }

    $InstalledExe = Join-Path $InstallDir 'ASSDraw3.exe'
    Assert-FileHashMatches -Path $InstalledExe
    $Uninstaller = Get-ChildItem -LiteralPath $InstallDir -Filter 'unins*.exe' -File | Select-Object -First 1
    if (!$Uninstaller) {
        throw 'Installer smoke test could not find the generated uninstaller.'
    }
    $UninstallProcess = Start-Process -FilePath $Uninstaller.FullName -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
    ) -Wait -PassThru
    if ($UninstallProcess.ExitCode -ne 0) {
        throw "Uninstaller smoke test failed with exit code $($UninstallProcess.ExitCode)."
    }
    if (Test-Path -LiteralPath $InstalledExe) {
        throw 'The uninstaller did not remove ASSDraw3.exe.'
    }
}
finally {
    Remove-Item -LiteralPath $InstallSmokeRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "Verified staged ASSDraw SHA256 $ExpectedHash in portable staging/archive and the installed/uninstalled application."
Write-Output "ASSDraw imports: $($Imports -join ', ')"
