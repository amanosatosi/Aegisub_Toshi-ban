"""Cache only dependency sources, resolving floating revisions before keying."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

DEPENDENCIES = ('ffmpeg', 'ffms2', 'soundtouch', 'dav1d')


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def resolve_revision(url, revision):
    if re.fullmatch(r'[0-9a-fA-F]{40}', revision):
        return revision.lower()
    if revision.lower() == 'head':
        refs = ['HEAD']
    elif revision.startswith('refs/'):
        refs = [revision + '^{}', revision]
    else:
        refs = ['refs/heads/' + revision, 'refs/tags/' + revision + '^{}', 'refs/tags/' + revision]
    output = run('git', 'ls-remote', url, *refs)
    resolved = dict((ref, sha) for sha, ref in (line.split() for line in output.splitlines()))
    for ref in refs:
        if ref in resolved and re.fullmatch(r'[0-9a-f]{40}', resolved[ref]):
            return resolved[ref]
    raise RuntimeError(f'Cannot resolve floating dependency {url} {revision}; refusing stale sources')


def prepare(root, metadata):
    subprojects = root / 'subprojects'
    inputs, revisions = {}, {}

    def fingerprint(path):
        path = path.resolve()
        if not path.is_relative_to(subprojects.resolve()):
            raise RuntimeError(f'Source input escaped subprojects: {path}')
        inputs[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()

    for name in DEPENDENCIES:
        wrap_path = subprojects / (name + '.wrap')
        fingerprint(wrap_path)
        content = wrap_path.read_text(encoding='utf-8')
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read_string(content)
        section = wrap[wrap.sections()[0]]
        if section.name == 'wrap-git':
            revision = resolve_revision(section['url'], section['revision'])
            revisions[name] = revision
            # Pin this ephemeral CI checkout to the same commit used in the key.
            # A remote ref moving during configuration cannot invalidate the key.
            content = re.sub(r'^revision\s*=.*$', 'revision = ' + revision, content, flags=re.MULTILINE)
            wrap_path.write_text(content, encoding='utf-8')
            print(f'{name}: {section["revision"]} -> {revision}', flush=True)
        if 'patch_directory' in section:
            overlay = subprojects / 'packagefiles' / section['patch_directory']
            for path in sorted(overlay.rglob('*')):
                if path.is_file():
                    fingerprint(path)
        for diff in section.get('diff_files', '').split(','):
            if diff.strip():
                fingerprint(subprojects / 'packagefiles' / diff.strip())

    digest = hashlib.sha256(json.dumps([inputs, revisions], sort_keys=True).encode()).hexdigest()
    metadata.write_text(json.dumps({'revisions': revisions, 'fingerprint': digest}), encoding='utf-8')
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('fingerprint=' + digest + '\n')
    return digest


def source_paths(root):
    subprojects = (root / 'subprojects').resolve()
    paths = []
    for name in DEPENDENCIES:
        path = subprojects / name
        if path.is_dir():
            if path.resolve().parent != subprojects:
                raise RuntimeError(f'Unexpected cached dependency path: {path}')
            paths.append(path)
    return paths


def refresh(root):
    paths = source_paths(root)
    for path in paths:
        if (path / '.git').exists():
            # Remove obsolete untracked overlays, including files deleted from
            # the new overlay. This operates only inside restored CI sources.
            subprocess.run(['git', '-C', str(path), 'clean', '-ffdx'], check=True)
    if paths:
        subprocess.run(['meson', 'subprojects', 'update', '--reset', *(p.name for p in paths)],
                       cwd=root, check=True)


def validate(root, metadata):
    revisions = json.loads(metadata.read_text(encoding='utf-8'))['revisions']
    for path in source_paths(root):
        if path.name in revisions:
            actual = run('git', '-C', str(path), 'rev-parse', 'HEAD')
            expected = revisions[path.name]
            if actual != expected:
                raise RuntimeError(f'{path.name}: source cache has {actual}, expected {expected}')
            print(f'{path.name}: validated source commit {actual}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'refresh', 'validate'])
    parser.add_argument('--metadata', type=Path, default=Path(os.environ['RUNNER_TEMP']) / 'meson-source-cache.json')
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if args.mode == 'prepare':
        prepare(root, args.metadata)
    elif args.mode == 'refresh':
        refresh(root)
    else:
        validate(root, args.metadata)
