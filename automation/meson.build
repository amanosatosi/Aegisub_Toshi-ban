subdir('include')
subdir('vapoursynth')

automation_dir = dataroot / 'automation'

bundled_autoload_names = [
    'cleantags-autoload.lua',
    'karaoke-auto-leadin.lua',
    'kara-templater.lua',
    'macro-1-edgeblur.lua',
    'macro-2-mkfullwitdh.lua',
    'select-overlaps.moon',
    'strip-tags.lua',
]
bundled_autoload_files = []
foreach name : bundled_autoload_names
    bundled_autoload_files += 'autoload' / name
endforeach
install_data(bundled_autoload_files, install_dir: automation_dir / 'autoload')
bundled_autoload_conf = configuration_data()
bundled_autoload_conf.set('BUNDLED_AUTOLOAD_NAMES', '"' + '", "'.join(bundled_autoload_names) + '"')
configure_file(output: 'bundled_automation.h', configuration: bundled_autoload_conf)

install_data(
    'demos/future-windy-blur.lua',
    'demos/raytracer.lua',
    'demos/raytracer-test1.ass',
    install_dir: automation_dir / 'demos')

install_data(
    'include/cleantags.lua',
    'include/clipboard.lua',
    'include/karaskel-auto4.lua',
    'include/karaskel.lua',
    'include/mangetsu-karaskel.lua',
    'include/kCompatibility.lua',
    'include/lfs.lua',
    'include/moonscript.lua',
    'include/re.lua',
    'include/unicode.lua',
    'include/unicode-monkeypatch.lua',
    'include/utils-auto4.lua',
    'include/utils.lua',
    install_dir: automation_dir / 'include')

install_data(
    'include/aegisub/argcheck.moon',
    'include/aegisub/clipboard.lua',
    'include/aegisub/ffi.moon',
    'include/aegisub/lfs.moon',
    'include/aegisub/re.moon',
    'include/aegisub/unicode.moon',
    'include/aegisub/util.moon',
    install_dir: automation_dir / 'include' / 'aegisub')

install_data(
    'vapoursynth/aegisub_vs.py',
    install_dir: automation_dir / 'vapoursynth')
