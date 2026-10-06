"""Deterministic CI-cache tests, executed only in GitHub Actions."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/ci' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cache = load('meson_source_cache')
stats = load('sccache_stats')
verify = load('verify_msvc_cache')


class SourceCache(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.sources = self.root / 'subprojects'
        self.sources.mkdir()
        self.metadata = self.root / 'metadata.json'
        self.environment = patch.dict(os.environ, GITHUB_OUTPUT=str(self.root / 'outputs'))
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.originals = {}
        for name in cache.DEPENDENCIES:
            content = ('[wrap-git]\nurl = https://example.invalid/' + name + '\nrevision = ' +
                       ('head' if name == 'dav1d' else 'a' * 40) + '\n')
            if name == 'ffms2':
                content += 'patch_directory = ffms2\n'
                overlay = self.sources / 'packagefiles/ffms2'
                overlay.mkdir(parents=True)
                (overlay / 'meson.build').write_text('overlay one')
            path = self.sources / (name + '.wrap')
            self.originals[path] = content
            path.write_text(content)

    def prepare(self, head='b' * 40):
        for path, content in self.originals.items():
            path.write_text(content)
        with patch.object(cache, 'run', return_value=head), contextlib.redirect_stdout(io.StringIO()):
            return cache.prepare(self.root, self.metadata)

    def test_floating_ref_movement_invalidates_key_and_pins_ci_checkout(self):
        first = self.prepare()
        self.assertIn('revision = ' + 'b' * 40, (self.sources / 'dav1d.wrap').read_text())
        second = self.prepare('c' * 40)
        self.assertNotEqual(first, second)
        self.assertEqual(json.loads(self.metadata.read_text())['revisions']['dav1d'], 'c' * 40)

    def test_overlay_changes_invalidate_but_application_edits_do_not(self):
        first = self.prepare()
        (self.root / 'application.cpp').write_text('ordinary code edit')
        self.assertEqual(first, self.prepare())
        (self.sources / 'packagefiles/ffms2/meson.build').write_text('overlay two')
        self.assertNotEqual(first, self.prepare())

    def test_unresolvable_floating_ref_fails_instead_of_using_stale_source(self):
        with patch.object(cache, 'run', return_value=''):
            with self.assertRaisesRegex(RuntimeError, 'refusing stale sources'):
                cache.resolve_revision('https://example.invalid/repo', 'head')

    def test_annotated_tag_uses_peeled_commit(self):
        output = 'a' * 40 + '\trefs/tags/v1\n' + 'b' * 40 + '\trefs/tags/v1^{}'
        with patch.object(cache, 'run', return_value=output):
            self.assertEqual(cache.resolve_revision('https://example.invalid/repo', 'v1'), 'b' * 40)

    def test_partial_restore_cleans_deleted_overlays_then_resets_all_sources(self):
        directory = self.sources / 'ffms2'
        directory.mkdir()
        def git(*args):
            subprocess.run(['git', '-C', str(directory), *args], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        git('init')
        git('config', 'user.email', 'ci@example.invalid')
        git('config', 'user.name', 'CI fixture')
        (directory / 'tracked').write_text('upstream')
        git('add', 'tracked')
        git('commit', '-m', 'fixture')
        obsolete = directory / 'obsolete-overlay'
        obsolete.write_text('old overlay')
        real_run = subprocess.run
        calls = []
        def execute(args, **kwargs):
            if args[0] == 'meson':
                calls.append(args)
                return subprocess.CompletedProcess(args, 0)
            return real_run(args, **kwargs)
        with patch.object(cache.subprocess, 'run', side_effect=execute):
            cache.refresh(self.root)
        self.assertFalse(obsolete.exists())
        self.assertTrue((directory / 'tracked').exists())
        self.assertEqual(calls, [['meson', 'subprojects', 'update', '--reset', 'ffms2']])

    def test_source_symlink_cannot_escape_restored_subprojects(self):
        outside = self.root / 'unrelated'
        outside.mkdir()
        (self.sources / 'ffms2').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected cached dependency path'):
            cache.refresh(self.root)


class CacheDiagnostics(unittest.TestCase):
    def info(self, errors, writes):
        return {'cache_location': 'ghac', 'basedirs': ['workspace'], 'stats': {
            'compile_requests': 120, 'cache_hits': {'counts': {'C/C++': 75}},
            'cache_misses': {'counts': {'C/C++': 25}}, 'cache_write_errors': errors,
            'cache_writes': writes, 'non_cacheable_compilations': 2, 'requests_not_cacheable': 18}}

    def test_read_only_retains_raw_errors_and_reports_no_backend_uploads(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result = stats.summary(self.info(25, 0), 'READ_ONLY', 'ReadOnly')
        self.assertEqual(result['Cache hit percentage'], '75.00%')
        self.assertEqual(result['Cache write errors (raw sccache counter)'], 25)
        self.assertEqual(result['GHA upload errors'], 0)

    def test_writer_errors_are_not_hidden_and_mode_mismatch_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result = stats.summary(self.info(3, 22), 'READ_WRITE', 'ReadWrite')
            self.assertEqual(result['GHA upload errors'], 3)
            with self.assertRaisesRegex(RuntimeError, 'fell back'):
                stats.summary(self.info(0, 0), 'READ_WRITE', 'ReadOnly')

    def test_requires_sccache_and_cl_not_merely_one_of_them(self):
        self.assertTrue(verify.wrapped('"C:/tools/sccache.exe" cl /c source.cpp'))
        self.assertTrue(verify.wrapped('sccache "C:/MSVC/bin/cl.exe" /c source.c'))
        self.assertFalse(verify.wrapped('cl /c source.c'))
        self.assertFalse(verify.wrapped('sccache clang /c source.cpp'))


if __name__ == '__main__':
    unittest.main()
