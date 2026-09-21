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

foreach ($Field in @('repository', 'source_commit', 'version', 'asset', 'url', 'sha256')) {
    if ([string]::IsNullOrWhiteSpace([string]$Metadata.$Field)) {
        throw "ASSDraw release metadata '$Field' is not configured in $MetadataPath. Publish a stable ASSDraw release, then pin its tag, direct ASSDraw3.exe URL, and SHA256."
    }
}

if ($Metadata.repository -cne 'https://github.com/amanosatosi/assdraw') {
    throw "Unexpected ASSDraw repository '$($Metadata.repository)'."
}
if ($Metadata.source_commit -notmatch '^[0-9a-f]{40}$') {
    throw "ASSDraw source_commit must be a full lowercase Git commit hash."
}
if ($Metadata.asset -cne 'ASSDraw3.exe') {
    throw "The ASSDraw release asset must be the single executable ASSDraw3.exe."
}
if ($Metadata.url -match '/releases/latest/' -or $Metadata.url -match '/latest/download/') {
    throw "ASSDraw must use a version-pinned release URL, not a floating latest URL."
}
$ExpectedUrl = "https://github.com/amanosatosi/assdraw/releases/download/$($Metadata.version)/ASSDraw3.exe"
if ($Metadata.url -cne $ExpectedUrl) {
    throw "ASSDraw URL must be the pinned release asset $ExpectedUrl."
}
if ($Metadata.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "ASSDraw sha256 must contain exactly 64 hexadecimal characters."
}

$DestinationDir = Join-Path $BuildRoot 'installer-deps\assdraw'
$Destination = Join-Path $DestinationDir 'ASSDraw3.exe'
$Temporary = "$Destination.download"
New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null

function Assert-AssDrawHash {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $Expected = ([string]$Metadata.sha256).ToLowerInvariant()
    if ($Actual -cne $Expected) {
        throw "SHA256 mismatch for $Path. Expected $Expected, got $Actual."
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

if (Test-Path -LiteralPath $Destination) {
    Assert-AssDrawHash -Path $Destination
    Assert-WindowsExecutable -Path $Destination
    Write-Output "Using verified ASSDraw $($Metadata.version) from $Destination"
    return
}

$Headers = @{}
if (Test-Path 'Env:GITHUB_TOKEN') {
    $Headers.Authorization = 'Bearer ' + $Env:GITHUB_TOKEN
}

try {
    for ($Attempt = 1; $Attempt -le 5; ++$Attempt) {
        try {
            if ($Headers.Count) {
                Invoke-WebRequest -Uri $Metadata.url -OutFile $Temporary -UseBasicParsing -Headers $Headers
            }
            else {
                Invoke-WebRequest -Uri $Metadata.url -OutFile $Temporary -UseBasicParsing
            }
            break
        }
        catch {
            Remove-Item -LiteralPath $Temporary -Force -ErrorAction SilentlyContinue
            if ($Attempt -eq 5) { throw }
            $Delay = [math]::Min(120, 5 * [math]::Pow(2, $Attempt - 1))
            Write-Output "ASSDraw download failed (attempt $Attempt of 5); retrying in $Delay seconds."
            Start-Sleep -Seconds $Delay
        }
    }

    Assert-AssDrawHash -Path $Temporary
    Assert-WindowsExecutable -Path $Temporary
    Move-Item -LiteralPath $Temporary -Destination $Destination
}
finally {
    Remove-Item -LiteralPath $Temporary -Force -ErrorAction SilentlyContinue
}

Write-Output "Downloaded and verified ASSDraw $($Metadata.version) ($($Metadata.source_commit))."
