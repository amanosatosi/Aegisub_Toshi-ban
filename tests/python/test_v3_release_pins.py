"""Protect the V3.0 release dependency and version contracts."""

import configparser
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class V3ReleasePins(unittest.TestCase):
    def test_all_meson_git_sources_are_immutable(self):
        for path in sorted((ROOT / "subprojects").glob("*.wrap")):
            config = configparser.ConfigParser(interpolation=None)
            config.read(path, encoding="utf-8")
            if not config.has_section("wrap-git"):
                continue
            revision = config["wrap-git"].get("revision", "")
            with self.subTest(wrap=path.name):
                self.assertRegex(revision, r"\A[0-9a-f]{40}\Z")

    def test_assdraw_checkout_is_locked(self):
        metadata = json.loads(
            (ROOT / "packages/win_installer/assdraw.json").read_text(encoding="utf-8")
        )
        self.assertRegex(metadata["commit"], r"\A[0-9a-f]{40}\Z")
        source = (ROOT / "tools/build-assdraw.ps1").read_text(encoding="utf-8")
        self.assertIn("fetch --no-tags --depth=1 origin $Metadata.commit", source)
        self.assertIn("$CheckedOutCommit -cne $Metadata.commit", source)

    def test_assdraw_script_has_one_valid_pin_guard_and_build(self):
        source = (ROOT / "tools/build-assdraw.ps1").read_text(encoding="utf-8")
        self.assertEqual(1, source.count("$AssDrawSource = Join-Path $BuildRoot 'assdraw-src'"))
        self.assertEqual(1, source.count("cmake --build $AssDrawBuild"))
        self.assertEqual(1, source.count("ASSDraw built and staged successfully."))
        self.assertIn("if ([string]$Metadata.commit -cnotmatch '^[0-9a-f]{40}$') {", source)
        self.assertIn("fetch --no-tags --depth=1 origin $Metadata.commit", source)
        self.assertNotIn("fetch --no-tags --depth=1 origin $Metadata.branch", source)

    def test_release_display_version_preserves_git_hash(self):
        source = (ROOT / "src/version.cpp").read_text(encoding="utf-8")
        self.assertIn('"Toshi-ban v3.0"', source)
        self.assertIn("return BUILD_GIT_HASH;", source)
        for path in ("tools/version.ps1", "tools/version.sh"):
            content = (ROOT / path).read_text(encoding="utf-8")
            self.assertIn("3.0.0", content)
        setup = (ROOT / "packages/win_installer/fragment_setupbase.iss").read_text(encoding="utf-8")
        self.assertIn("AppVersion=3.0.0", setup)
        self.assertIn("AppVerName=Aegisub Toshi-ban v3.0", setup)


if __name__ == "__main__":
    unittest.main()
