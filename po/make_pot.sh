#!/bin/sh
# Run from any directory. Requires Python 3 and GNU gettext (including Lua support).
set -eu
cd "$(dirname "$0")"

# POTFILES is also used by Meson's built-in gettext target. Keep it current,
# but use the commands below for the complete catalog (JSON, Lua and installer).
"${PYTHON:-python3}" update_potfiles.py
xgettext --directory=.. --files-from=POTFILES -o aegisub.pot --language=C++ --from-code=UTF-8 \
  --package-name=Aegisub --package-version=Toshi-ban \
  --sort-by-file -k_ -kSTR_MENU -kSTR_DISP -kSTR_HELP -kfmt_tl \
  -kfmt_plural:2,3 -kwxPLURAL:1,2 -kwxTRANSLATE -kmotion_message

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${PYTHON:-python3}" extract_resources.py > "$tmp/resources.cpp"
xgettext -o "$tmp/resources.pot" --omit-header --language=C++ --from-code=UTF-8 \
  --add-location=never -k_ "$tmp/resources.cpp"
msgcat --use-first -o "$tmp/merged.pot" aegisub.pot "$tmp/resources.pot"
mv "$tmp/merged.pot" aegisub.pot
find ../automation -name '*.lua' -type f | LC_ALL=C sort > "$tmp/lua-files"
if [ -s "$tmp/lua-files" ]; then
  xgettext --join-existing -o aegisub.pot --language=Lua --from-code=UTF-8 \
    -ktr --files-from="$tmp/lua-files"
fi
xgettext --join-existing -o aegisub.pot --language=Desktop ../packages/desktop/aegisub.desktop.in.in
xgettext --join-existing -o aegisub.pot --language=AppData ../packages/desktop/aegisub.appdata.xml.in.in

# Updating translations is intentionally separate: msgmerge --update --backup=none
# --no-fuzzy-matching LANGUAGE.po aegisub.pot
