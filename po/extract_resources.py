#!/usr/bin/env python3
"""Turn runtime-translated resource labels into xgettext input, not IDs."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
messages = set()

def menu(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key == 'text' and isinstance(child, str):
                messages.add(child)
            else:
                menu(child)
    elif isinstance(value, list):
        for child in value:
            menu(child)

for path in (root / 'src' / 'libresrc').rglob('default_menu.json'):
    menu(json.loads(path.read_text(encoding='utf-8-sig')))
hotkeys = json.loads((root / 'src/libresrc/default_hotkey.json').read_text(encoding='utf-8-sig'))
messages.update(hotkeys.keys())
for line in (root / 'packages/win_installer/fragment_strings.iss.in').read_text(encoding='utf-8-sig').splitlines():
    key, separator, value = line.partition('=')
    if separator and key.startswith('_') and key[1:].isalnum():
        messages.add(value)
for message in sorted(messages):
    print('_(' + json.dumps(message, ensure_ascii=False) + ');')
