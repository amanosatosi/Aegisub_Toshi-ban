# Translating Aegisub

Mark user-visible C++ text with `_()`, `STR_MENU`, `STR_DISP`, `STR_HELP`,
`fmt_tl`, or `fmt_plural` as appropriate. Do not translate command IDs, ASS
tags, configuration keys, file extensions, or other machine-readable values.
Menu JSON `text` fields and top-level hotkey context names are translated at
runtime. Mark bundled Lua strings with `tr` (both `tr"..."` and `tr("...")`
are supported). Dynamic strings must be translated before they are displayed.

Run `po/make_pot.sh` with Python 3 and GNU gettext installed. It refreshes
`POTFILES` from all C++ headers and sources, then extracts JSON, bundled Lua,
desktop metadata and installer strings. Run this script instead of Meson's
`pot` target, which only covers C++. Update only the catalogs being worked on:
`msgmerge --update --backup=none --no-fuzzy-matching po/my.po po/aegisub.pot`.
Validate with `msgfmt --check --check-format -o /tmp/aegisub.mo po/my.po`.
Meson compiles and installs catalogs listed in `LINGUAS`; the Windows installer
also needs an entry in `fragment_translations.iss`. Portable builds copy the
Meson-installed locale tree. A missing wxWidgets standard catalog leaves wx
standard strings in English; it does not replace Aegisub's catalog.

For Burmese use Myanmar Unicode and the fansubbing terms စာဖိုင် (subtitle
file), အသံဖိုင် (audio file), and ဗီဒီယိုဖိုင် (video file). Distinguish a
subtitle file from subtitle text or lines. Preserve placeholders, access keys,
and technical identifiers. Leave uncertain strings untranslated for review.
The initial Burmese catalog is incomplete and needs native-speaker review,
especially for specialist terminology, layout and complex-script rendering.
