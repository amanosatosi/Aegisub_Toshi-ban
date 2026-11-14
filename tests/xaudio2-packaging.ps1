$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$setup = Get-Content -Raw (Join-Path $repo 'tools/win-installer-setup.ps1')
# Run the production staging block without downloading other installer dependencies.
$stage = [scriptblock]::Create([regex]::Match($setup, '(?s)# XAudio2 redistributable\r?\n(.*?)# dictionaries').Groups[1].Value)
$root = Join-Path ([IO.Path]::GetTempPath()) ("xaudio-ci-" + [guid]::NewGuid())
$relativeDll = 'build/native/release/bin/x64/xaudio2_9redist.dll'
$fixture = Join-Path $root 'fixture'
$fixtureDll = Join-Path $fixture $relativeDll
$zip = Join-Path $root 'fixture.zip'
$ResolvedDepsDir = Join-Path $root 'installer-deps'
$staged = Join-Path $ResolvedDepsDir "XAudio2_redist/$relativeDll"
$portable = Join-Path $repo 'packages/win_installer/portable/create-portable.ps1'

function Invoke-WebRequestWithRetry($Uri, $OutFile) {
    if ($Uri -cne 'https://www.nuget.org/api/v2/package/Microsoft.XAudio2.Redist/1.2.11') {
        throw "Unexpected NuGet URL: $Uri"
    }
    $script:downloads++
    Copy-Item -LiteralPath $zip -Destination $OutFile
}

function Assert-Throws($action, $pattern) {
    try { & $action }
    catch {
        if ($_.Exception.Message -match $pattern) { return }
        throw
    }
    throw "Expected failure matching $pattern"
}

try {
    New-Item -ItemType Directory -Path (Split-Path -Parent $fixtureDll), $ResolvedDepsDir | Out-Null
    Set-Content -LiteralPath $fixtureDll -Value 'fixture DLL'
    Compress-Archive -Path (Join-Path $fixture '*') -DestinationPath $zip
    $script:downloads = 0
    Assert-Throws { & $portable $root $repo } 'Required XAudio2 runtime DLL is missing'
    & $stage
    if (!(Test-Path -LiteralPath $staged -PathType Leaf)) { throw 'Fresh staging failed' }
    & $stage
    if ($downloads -ne 1) { throw 'Complete staging was downloaded again' }
    Remove-Item -LiteralPath $staged
    & $stage
    if ($downloads -ne 2 -or (Get-Content -LiteralPath $staged) -ne 'fixture DLL') {
        throw 'Incomplete staging was not repaired'
    }
    Remove-Item -LiteralPath $staged
    Set-Content -LiteralPath $zip -Value 'invalid archive'
    Assert-Throws $stage 'XAudio2 extraction failed with exit code'
    if (Test-Path -LiteralPath (Join-Path $ResolvedDepsDir 'XAudio2Redist.zip')) {
        throw 'Temporary archive was not cleaned up'
    }
    Write-Output 'XAudio2 packaging fixtures passed'
}
finally {
    if (Test-Path -LiteralPath $root) {
        $resolvedRoot = (Resolve-Path -LiteralPath $root).ProviderPath
        if ($resolvedRoot -eq $root) { Remove-Item -LiteralPath $resolvedRoot -Recurse -Force }
    }
}
