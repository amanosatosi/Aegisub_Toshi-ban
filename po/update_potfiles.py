#!/usr/bin/env python3
"""Keep the Meson C++ extraction input list in sync with the source tree."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sources = sorted(p.relative_to(root).as_posix() for p in (root / 'src').rglob('*')
                 if p.is_file() and p.suffix in ('.cpp', '.h'))
(root / 'po' / 'POTFILES').write_text(''.join(s + '\n' for s in sources), encoding='utf-8')
