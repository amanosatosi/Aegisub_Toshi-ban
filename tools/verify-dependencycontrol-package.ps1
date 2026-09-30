param(
    [Parameter(Mandatory = $true)][string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$SourceRoot
)
$ErrorActionPreference = 'Stop'
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).ProviderPath
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).ProviderPath
$DepCtrlRoot = Join-Path $BuildRoot 'installer-deps\DependencyControl'
$PortableRoot = Join-Path $BuildRoot 'aegisub-portable'
if ((git -C $DepCtrlRoot describe --tags --exact-match) -cne 'v0.9.0') {
    throw 'DependencyControl source is not pinned to v0.9.0.'
}
git -C $DepCtrlRoot diff --exit-code -- modules macros
if ($LASTEXITCODE -ne 0) { throw 'DependencyControl contains local source patches.' }

function Assert-TreeMatches {
    param([string]$Source, [string]$Installed)
    $expected = @(Get-ChildItem -LiteralPath $Source -Recurse -File)
    $actual = @(Get-ChildItem -LiteralPath $Installed -Recurse -File)
    if ($expected.Count -ne $actual.Count) { throw "Payload file count differs: $Installed" }
    foreach ($file in $expected) {
        $relative = $file.FullName.Substring($Source.Length).TrimStart('\')
        $target = Join-Path $Installed $relative
        if (!(Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing packaged file: $target" }
        if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Packaged file differs from upstream: $target"
        }
    }
}

Assert-TreeMatches "$DepCtrlRoot\modules\l0" "$PortableRoot\automation\include\l0"
Assert-TreeMatches "$DepCtrlRoot\macros\l0.DependencyControl.Toolbox" "$PortableRoot\automation\autoload\l0.DependencyControl.Toolbox"
if ((Get-FileHash "$DepCtrlRoot\macros\l0.DependencyControl.Toolbox.moon").Hash -ne
    (Get-FileHash "$PortableRoot\automation\autoload\l0.DependencyControl.Toolbox.moon").Hash) {
    throw 'Portable Toolbox entry differs from upstream.'
}
$retired = @(
    'automation\include\l0\l0', 'automation\autoload\garret.depctrl_config.lua',
    'automation\include\requireffi\requireffi.lua', 'automation\include\BM\BadMutex.lua',
    'automation\include\PT\PreciseTimer.lua', 'automation\include\DM\DownloadManager.lua'
)
foreach ($path in $retired) {
    if (Test-Path -LiteralPath (Join-Path $PortableRoot $path)) { throw "Retired portable payload: $path" }
}
$zipList = 7z l "$BuildRoot\aegisub-portable-64.zip"
if ($LASTEXITCODE -ne 0 -or !($zipList -match 'DependencyControl\.moon') -or
    $zipList -match 'garret\.depctrl_config\.lua|BadMutex\.dll|PreciseTimer\.dll|DownloadManager\.dll') {
    throw 'Portable archive has a missing DependencyControl entry or retired payload.'
}

# Compile a small installer from the actual Automation fragment. Redirect only
# userappdata into an isolated CI sandbox; production cleanup/copy rules remain
# identical. The full Windows installer is already compiled by the build job.
$Sandbox = Join-Path $BuildRoot 'depctrl-installer-regression'
$Profile = Join-Path $Sandbox 'profile'
$App = Join-Path $Sandbox 'app'
New-Item -ItemType Directory -Path $Sandbox -Force | Out-Null
$fragment = Get-Content "$SourceRoot\packages\win_installer\fragment_automation.iss" -Raw
$fragment.Replace('{userappdata}', $Profile) | Set-Content "$Sandbox\automation.iss" -Encoding utf8
$harness = @"
#define SOURCE_ROOT "$SourceRoot"
#define DEPS_DIR "$BuildRoot\installer-deps"
#define DEPCTRL
[Setup]
AppName=DependencyControl packaging regression
AppVersion=1.0
DefaultDirName=$App
PrivilegesRequired=lowest
Uninstallable=no
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=$Sandbox
OutputBaseFilename=depctrl-regression
[Types]
Name: full; Description: Full
[Components]
Name: main; Description: Main; Types: full; Flags: fixed
Name: macros; Description: Automation; Types: full
Name: macros\bundled; Description: Bundled; Types: full
Name: macros\demos; Description: Demos; Types: full
Name: macros\modules; Description: Modules; Types: full
Name: macros\modules\depctrl; Description: DependencyControl; Types: full
Name: macros\modules\yutils; Description: Yutils; Types: full
Name: macros\modules\luajson; Description: JSON; Types: full
#include "automation.iss"
"@
$harness | Set-Content "$Sandbox\regression.iss" -Encoding utf8
iscc "$Sandbox\regression.iss"
if ($LASTEXITCODE -ne 0) { throw 'DependencyControl installer regression harness did not compile.' }

function Install-Fixture {
    param([string]$Components = 'main,macros,macros\bundled,macros\demos,macros\modules,macros\modules\depctrl,macros\modules\yutils,macros\modules\luajson')
    $process = Start-Process -FilePath "$Sandbox\depctrl-regression.exe" -WindowStyle Hidden -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/COMPONENTS=$Components"
    )
    if ($process.ExitCode -ne 0) { throw "DependencyControl fixture install failed: $($process.ExitCode)" }
}

function Seed-File {
    param([string]$Path, [string]$Contents = 'obsolete bundled code')
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    Set-Content -LiteralPath $Path -Value $Contents -NoNewline
}

function Assert-CleanInstallation {
    Assert-TreeMatches "$DepCtrlRoot\modules\l0\DependencyControl" "$Profile\Aegisub\automation\include\l0\DependencyControl"
    Assert-TreeMatches "$DepCtrlRoot\macros\l0.DependencyControl.Toolbox" "$Profile\Aegisub\automation\autoload\l0.DependencyControl.Toolbox"
    foreach ($entry in @('include\l0\DependencyControl.moon', 'include\l0\dkjson.moon', 'autoload\l0.DependencyControl.Toolbox.moon')) {
        if (!(Test-Path "$Profile\Aegisub\automation\$entry")) { throw "Missing installed module: $entry" }
    }
    foreach ($entry in @('include\l0\DependencyControl.lua', 'autoload\l0.DependencyControl.Toolbox.lua',
        'autoload\garret.depctrl_config.lua', 'include\requireffi\requireffi.lua',
        'include\BM\BadMutex.lua', 'include\BM\BadMutex\BadMutex.dll',
        'include\PT\PreciseTimer.lua', 'include\PT\PreciseTimer\PreciseTimer.dll',
        'include\DM\DownloadManager.lua', 'include\DM\DownloadManager\DownloadManager.dll')) {
        if (Test-Path "$Profile\Aegisub\automation\$entry") { throw "Retired installed code: $entry" }
    }
    if (Test-Path "$App\automation\autoload\garret.depctrl_config.lua") { throw 'Retired app macro retained.' }
}

# Fresh install.
Install-Fixture
Assert-CleanInstallation

# Upgrade fixtures include stale renamed modules, Toolbox subfiles and helpers.
$oldConfig = "$Profile\Aegisub\DependencyControl.json"
$newConfig = "$Profile\Aegisub\config\l0.DependencyControl.json"
Seed-File $oldConfig '{"config":{"updaterEnabled":false},"marker":"legacy state"}'
$oldHash = (Get-FileHash $oldConfig).Hash
foreach ($entry in @('include\l0\DependencyControl.lua', 'include\l0\DependencyControl\FileOps.moon',
    'autoload\l0.DependencyControl.Toolbox.lua', 'autoload\l0.DependencyControl.Toolbox\obsolete.moon',
    'autoload\garret.depctrl_config.lua', 'include\requireffi\requireffi.lua',
    'include\BM\BadMutex.lua', 'include\BM\BadMutex\BadMutex.dll',
    'include\PT\PreciseTimer.lua', 'include\PT\PreciseTimer\PreciseTimer.dll',
    'include\DM\DownloadManager.lua', 'include\DM\DownloadManager\DownloadManager.dll')) {
    Seed-File "$Profile\Aegisub\automation\$entry"
}
Seed-File "$App\automation\autoload\garret.depctrl_config.lua"
$unrelated = @('automation\autoload\user-script.lua', 'automation\include\BM\UserMutex.lua',
    'automation\include\l0\UserModule.moon', 'Nudge.json', 'l0.UpdateFeed_custom.json')
foreach ($entry in $unrelated) { Seed-File "$Profile\Aegisub\$entry" 'user state' }
Install-Fixture
Assert-CleanInstallation
if ((Get-FileHash $oldConfig).Hash -ne $oldHash -or (Get-FileHash $newConfig).Hash -ne $oldHash) {
    throw 'Upgrade must preserve legacy state and copy it unchanged for upstream migration.'
}
foreach ($entry in $unrelated) {
    if ((Get-Content "$Profile\Aegisub\$entry" -Raw) -cne 'user state') { throw "Unrelated user data changed: $entry" }
}

# Reinstall must preserve the now-current configuration over the old one.
Seed-File $newConfig '{"marker":"current state"}'
$newHash = (Get-FileHash $newConfig).Hash
Install-Fixture
Assert-CleanInstallation
if ((Get-FileHash $newConfig).Hash -ne $newHash -or (Get-FileHash $oldConfig).Hash -ne $oldHash) {
    throw 'Reinstall must preserve both configuration files.'
}

# Opting out of the component must not delete an existing DependencyControl copy.
Seed-File "$Profile\Aegisub\automation\include\l0\DependencyControl\keep.moon"
Install-Fixture -Components 'main'
if (!(Test-Path "$Profile\Aegisub\automation\include\l0\DependencyControl\keep.moon")) {
    throw 'An unselected DependencyControl component must not remove its existing copy.'
}
Write-Host 'DependencyControl portable payload and fresh/upgrade/reinstall installer regressions passed.'
