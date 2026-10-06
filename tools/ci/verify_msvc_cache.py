"""Check sccache/cl coverage and print comparable fallback command hashes."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def wrapped(command):
    return 'sccache' in command.lower() and re.search(r'(?:^|[\\/\s"])cl(?:\.exe)?(?=$|[\s"])', command, re.I)


def main():
    compilers = json.loads(subprocess.check_output(
        ['meson', 'introspect', 'build', '--compilers'], text=True))
    for language in ('c', 'cpp'):
        command = compilers['host'][language]['exelist']
        print(f'{language}: {command}', flush=True)
        if len(command) < 2 or Path(command[0]).stem.lower() != 'sccache' or Path(command[1]).stem.lower() != 'cl':
            raise RuntimeError(f'Meson {language} is not using sccache cl: {command}')

    entries = json.loads(Path('build/compile_commands.json').read_text(encoding='utf-8'))
    c_cpp = [e for e in entries if Path(e['file']).suffix.lower() in {'.c', '.cc', '.cpp', '.cxx'}]
    commands = lambda e: e.get('command') or ' '.join(e['arguments'])
    bypasses = [e for e in c_cpp if not wrapped(commands(e))]
    if bypasses:
        raise RuntimeError(f'{len(bypasses)} C/C++ commands bypass sccache cl: {bypasses[0]}')
    print(f'All {len(c_cpp)} main-project/fallback C/C++ commands use sccache cl.', flush=True)

    root = os.environ['GITHUB_WORKSPACE'].replace('\\', '/').lower()
    for dependency in ('ffmpeg', 'ffms2', 'soundtouch', 'zlib', 'harfbuzz', 'freetype2',
                       'fribidi', 'libpng', 'libassmod', 'libassmod-mangetsu'):
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read('subprojects/' + dependency + '.wrap')
        directory = wrap[wrap.sections()[0]].get('directory', dependency)
        marker = '/subprojects/' + directory.lower() + '/'
        selected = [e for e in c_cpp if marker in e['file'].replace('\\', '/').lower()]
        if not selected:
            raise RuntimeError(f'Expected C/C++ compiler commands for fallback {dependency}')
        normalized = sorted(commands(e).replace('\\', '/').lower().replace(root, '$WORKSPACE') for e in selected)
        digest = hashlib.sha256('\n'.join(normalized).encode()).hexdigest()[:16]
        print(f'{dependency}: {len(selected)} commands; command fingerprint {digest}', flush=True)
    print('Fingerprints diagnose shared flags; sccache hashes the complete compiler inputs for compatibility.')


if __name__ == '__main__':
    main()
