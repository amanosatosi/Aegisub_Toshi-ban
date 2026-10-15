"""Command isolation and the real respack path for the dedicated 39 Mode logo."""
from pathlib import Path
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SIZES = (16, 24, 32, 48, 64)
THEMES = ("button", "button_dark")


class Mode39IconControls(unittest.TestCase):
    def test_only_39_mode_uses_the_new_command_icon(self):
        source = (ROOT / "src/command/audio.cpp").read_text(encoding="utf-8-sig")
        command = source.split("struct audio_karaoke_39 final", 1)[1].split("struct ", 1)[0]
        self.assertIn('CMD_NAME("audio/karaoke/39")', command)
        self.assertIn("CMD_ICON(kara_39_mode)", command)
        old_mode = source.split("struct audio_karaoke_toshiki_ktiming final", 1)[1].split("struct ", 1)[0]
        self.assertIn("CMD_ICON(kara_spectrogram_timing)", old_mode)
        self.assertEqual(1, source.count("CMD_ICON(kara_39_mode)"))
        toolbar = json.loads((ROOT / "src/libresrc/default_toolbar.json").read_text())
        self.assertIn("audio/karaoke/39", toolbar["audio"])
        for path in ("src/libresrc/default_menu.json", "src/libresrc/osx/default_menu.json"):
            self.assertIn('"audio/karaoke/39"', (ROOT / path).read_text())

    def test_both_themes_package_transparent_bitmaps_at_all_command_sizes(self):
        manifest = (ROOT / "src/bitmaps/manifest.respack").read_text().splitlines()
        for size in SIZES:
            images = []
            for theme in THEMES:
                path = f"{theme}/kara_39_mode_{size}.png"
                self.assertEqual(1, manifest.count(path), path)
                data = (ROOT / "src/bitmaps" / path).read_bytes()
                self.assertEqual(b"\x89PNG\r\n\x1a\n", data[:8], path)
                self.assertEqual(b"IHDR", data[12:16], path)
                width, height, depth, color = struct.unpack(">IIBB", data[16:26])
                self.assertEqual((size, size, 8, 6), (width, height, depth, color), path)
                images.append(data)
                # The old resource remains required by the separate K-Timing command.
                self.assertEqual(1, manifest.count(f"{theme}/kara_spectrogram_timing_{size}.png"))
            self.assertEqual(images[0], images[1], "The authoritative logo is identical in both themes")

    def test_respack_generates_command_symbols_and_dark_lookup_entries(self):
        paths = [f"{theme}/kara_39_mode_{size}.png" for theme in THEMES for size in SIZES]
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            for path in paths:
                target = temporary / path
                target.parent.mkdir(exist_ok=True)
                shutil.copyfile(ROOT / "src/bitmaps" / path, target)
            manifest = temporary / "manifest.respack"
            manifest.write_text("\n".join(paths) + "\n")
            cpp, header = temporary / "bitmap.cpp", temporary / "bitmap.h"
            subprocess.run([sys.executable, str(ROOT / "tools/respack.py"), str(manifest),
                            str(cpp), str(header)], check=True, capture_output=True, text=True)
            declarations, table = header.read_text(), cpp.read_text()
            for size in SIZES:
                name = f"kara_39_mode_{size}"
                self.assertIn(f"extern const unsigned char {name}[", declarations)
                self.assertIn(f"extern const unsigned char button_dark_{name}[", declarations)
                self.assertIn(f'{{"{name}", "button/{name}.png", {name},', table)
                self.assertIn(f'{{"{name}", "button_dark/{name}.png", button_dark_{name},', table)


if __name__ == "__main__":
    unittest.main()
