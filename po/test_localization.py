#!/usr/bin/env python3
"""Smoke checks for extraction inputs, the catalog and Windows deployment."""
import json
import gettext
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parent.parent
po = root / 'po'
expected = sorted(p.relative_to(root).as_posix() for p in (root / 'src').rglob('*')
                  if p.is_file() and p.suffix in ('.cpp', '.h'))
assert (po / 'POTFILES').read_text(encoding='utf-8').splitlines() == expected
resources = subprocess.check_output([sys.executable, str(po / 'extract_resources.py')], text=True, encoding='utf-8')
assert json.dumps('&File') in resources
pot = (po / 'aegisub.pot').read_text(encoding='utf-8')
for message in ('Selected line frames: %d - %d', 'Make text fullwidth', '&File', 'Motion Track',
                'Active subtitle changed. Start a new tracking session.',
                'Motion Revert metadata is damaged or unsupported.',
                '%d frame-by-frame transforms; one event per subtitle',
                'No confident phonetic joins identified.',
                'OCR runtime returned JSON without a data field.'):
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
    assert translation.gettext('Open Audio File') == 'အသံဖိုင်ဖွင့်ရန်'
    for source, expected in {
        'Actor': 'နာမည်',
        'Undo': 'အုံဖွ စောနကလုပ်တာပျောက်စေ',
        'Redo': 'အုံဖွ စောနကပျောက်တာပြန်ပေါ်စေ',
        'Motion Track': 'လှုပ်ရှားမှုပုံတူကူးခြင်း',
        'Track Motion': 'လှုပ်ရှားမှု ပုံတူကူးရန်',
        'Save Subtitles as': 'နာမည်ပြောင်းပြီးစာဖိုင်သိမ်းရန်',
        'Export Subtitles': 'ပုံစံပြောင်းပြီးသိမ်းရန်',
        'Recent actors': 'အရင်ကသုံးတဲ့ နာမည်များ',
        'Open recent audio': 'အရင်ကသုံးတဲ့အသံဖိုင်များ',
        'Paste Lines Over': 'စာကြောင်းများ အစားထိုးကူးထည့်ရန်',
        'Reload Audio': 'အသံဖိုင် ပြန်တင်ရန်',
        'Reload Video': 'ဗီဒီယိုဖိုင် ပြန်တင်ရန်',
        'Revert': 'မူလအတိုင်း ပြန်ထားရန်',
        'Fast': 'အမြန်',
    }.items():
        assert translation.gettext(source) == expected, source
    assert translation.gettext('untranslated test message') == 'untranslated test message'
    assert translation.ngettext('Selection was set to one line', 'Selection was set to %u lines', 3) == 'စာကြောင်း %u ကြောင်း ရွေးထားသည်'
    # msgfmt checks flagged C formats. Also check placeholders in entries that
    # do not carry a format flag, and preserve literal ASS override tags.
    tokens = re.compile(r'%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[a-zA-Z%]|\{(?:\d+|[a-zA-Z_]\w*)?\}|\\(?:clip|iclip|movevc|[1234]?[a-zA-Z]+)')
    for original, translated in translation._catalog.items():
        if isinstance(original, str) and original:
            assert Counter(tokens.findall(original)) == Counter(tokens.findall(translated)), original
    fuzzy = subprocess.check_output(['msgattrib', '--only-fuzzy', '--no-obsolete',
                                     str(po / 'my.po')], text=True, encoding='utf-8')
    assert not re.search(r'^#,.*\bfuzzy\b', fuzzy, re.MULTILINE)
    untranslated = subprocess.check_output(['msgattrib', '--untranslated', '--no-obsolete',
                                            str(po / 'my.po')], text=True, encoding='utf-8')
    assert not re.search(r'^msgid ', untranslated, re.MULTILINE), 'Burmese catalog has untranslated entries'
print('Localization smoke checks passed')
