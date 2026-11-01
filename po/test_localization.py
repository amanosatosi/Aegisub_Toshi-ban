#!/usr/bin/env python3
"""Smoke checks for extraction inputs, the catalog and Windows deployment."""
import json
import gettext
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
po = root / 'po'
expected = sorted(p.relative_to(root).as_posix() for p in (root / 'src').rglob('*')
                  if p.is_file() and p.suffix in ('.cpp', '.h'))
assert (po / 'POTFILES').read_text(encoding='utf-8').splitlines() == expected
resources = subprocess.check_output([sys.executable, str(po / 'extract_resources.py')], text=True, encoding='utf-8')
assert json.dumps('&File') in resources
pot = (po / 'aegisub.pot').read_text(encoding='utf-8')
for message in ('Selected line frames: %d - %d', 'Make text fullwidth', '&File', 'Motion Track'):
    assert 'msgid ' + json.dumps(message) in pot, message
assert 'my' in (po / 'LINGUAS').read_text(encoding='utf-8').splitlines()
installer = (root / 'packages/win_installer/fragment_translations.iss').read_text(encoding='utf-8')
assert r'\po\my\LC_MESSAGES\aegisub.mo' in installer
assert r'{app}\locale\my' in installer
portable = (root / 'packages/win_installer/portable/create-portable.ps1').read_text(encoding='utf-8')
assert r'$InstallerDir\share\locale\*' in portable
with tempfile.TemporaryDirectory() as tmp:
    catalog = Path(tmp) / 'aegisub.mo'
    subprocess.run(['msgfmt', '--check', '--check-format', '-o', str(catalog),
                    str(po / 'my.po')], check=True)
    with catalog.open('rb') as source:
        translation = gettext.GNUTranslations(source)
    assert translation.gettext('Open Audio File') == 'အသံဖိုင် ဖွင့်မည်'
    assert translation.gettext('untranslated test message') == 'untranslated test message'
print('Localization smoke checks passed')
