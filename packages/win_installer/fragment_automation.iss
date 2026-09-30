; This file declares all installables related to Aegisub Automation

[Files]
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\cleantags-autoload.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\karaoke-auto-leadin.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\kara-templater.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\macro-1-edgeblur.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\macro-2-mkfullwitdh.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\select-overlaps.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled
DestDir: {app}\automation\autoload; Source: {#SOURCE_ROOT}\automation\autoload\strip-tags.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\bundled

DestDir: {app}\automation\demos; Source: {#SOURCE_ROOT}\automation\demos\future-windy-blur.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\demos
DestDir: {app}\automation\demos; Source: {#SOURCE_ROOT}\automation\demos\raytracer.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: macros\demos

DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\argcheck.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\clipboard.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\ffi.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\lfs.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\re.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\unicode.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include\aegisub; Source: {#SOURCE_ROOT}\automation\include\aegisub\util.moon; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main

DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\cleantags.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\clipboard.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\karaskel.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\karaskel-auto4.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\kCompatibility.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\lfs.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\moonscript.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\re.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\unicode.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\unicode-monkeypatch.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\utils.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main
DestDir: {app}\automation\include; Source: {#SOURCE_ROOT}\automation\include\utils-auto4.lua; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main

DestDir: {app}\automation\vapoursynth; Source: {#SOURCE_ROOT}\automation\vapoursynth\aegisub_vs.py; Flags: ignoreversion overwritereadonly uninsremovereadonly; Attribs: readonly; Components: main

#ifdef DEPCTRL
; DepCtrl
; Upstream migrates the JSON schema, but now reads a namespaced config path.
; Seed that path once, preserve the legacy file, and never overwrite newer state.
DestDir: {userappdata}\Aegisub\config; DestName: l0.DependencyControl.json; Source: {userappdata}\Aegisub\DependencyControl.json; Flags: external onlyifdoesntexist skipifsourcedoesntexist uninsneveruninstall; Components: macros\modules\depctrl
DestDir: {userappdata}\Aegisub\automation\include\l0; Source: {#DEPS_DIR}\DependencyControl\modules\l0\*; Flags: ignoreversion recursesubdirs createallsubdirs; Components: macros\modules\depctrl
DestDir: {userappdata}\Aegisub\automation\autoload; Source: {#DEPS_DIR}\DependencyControl\macros\*; Flags: ignoreversion recursesubdirs createallsubdirs; Components: macros\modules\depctrl
DestDir: {userappdata}\Aegisub\automation\include; Source: {#DEPS_DIR}\Yutils\src\Yutils.lua; Flags: ignoreversion; Components: macros\modules\yutils
DestDir: {userappdata}\Aegisub\automation\include; Source: {#DEPS_DIR}\luajson\lua\*; Flags: ignoreversion recursesubdirs createallsubdirs; Components: macros\modules\luajson
#endif

[InstallDelete]
; Retire our old macro in both locations without touching user configuration.
Type: files; Name: "{app}\automation\autoload\garret.depctrl_config.lua"
Type: files; Name: "{userappdata}\Aegisub\automation\autoload\garret.depctrl_config.lua"
#ifdef DEPCTRL
; Replace only the bundled code when DependencyControl is selected. Inno runs
; InstallDelete before Files, so fresh installs, upgrades and reinstalls agree.
; DependencyControl.json and the config/cache/test/schema state are preserved.
Type: files; Name: "{userappdata}\Aegisub\automation\include\l0\DependencyControl.moon"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\l0\DependencyControl.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: filesandordirs; Name: "{userappdata}\Aegisub\automation\include\l0\DependencyControl"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\autoload\l0.DependencyControl.Toolbox.moon"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\autoload\l0.DependencyControl.Toolbox.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: filesandordirs; Name: "{userappdata}\Aegisub\automation\autoload\l0.DependencyControl.Toolbox"; Check: WizardIsComponentSelected('macros\modules\depctrl')
; Exact legacy helper payloads only: never remove the BM/PT/DM namespaces.
Type: files; Name: "{userappdata}\Aegisub\automation\include\requireffi\requireffi.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\BM\BadMutex.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\BM\BadMutex\BadMutex.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\PT\PreciseTimer.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\PT\PreciseTimer\PreciseTimer.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\DM\DownloadManager.lua"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\DM\DownloadManager\DownloadManager.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\DM\DownloadManager.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\BM\BadMutex.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
Type: files; Name: "{userappdata}\Aegisub\automation\include\PT\PreciseTimer.dll"; Check: WizardIsComponentSelected('macros\modules\depctrl')
#endif
