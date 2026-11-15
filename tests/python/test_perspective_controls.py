"""wx perspective-mode wiring. ASS serialization is tested in C++."""
import json
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PerspectiveControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/visual_tool_perspective.cpp").read_text(encoding="utf-8-sig")
        cls.header = (ROOT / "src/visual_tool_perspective.h").read_text(encoding="utf-8-sig")
        cls.commands = (ROOT / "src/command/vis_tool.cpp").read_text(encoding="utf-8-sig")

    def method(self, name):
        match = re.search(r"VisualToolPerspective::" + name + r"\([^)]*\)[^{]*\{"
                          r"(.*?)\n\}", self.source, re.S)
        self.assertIsNotNone(match, name)
        return match.group(1)

    def test_two_modes_share_the_existing_tool(self):
        toolbar = self.method("SetToolbar")
        modes = re.findall(r'AddTool\("([^"]+)",\s*(PERSP_MODE_\w+)\)', toolbar)
        self.assertEqual([
            ("video/tool/perspective/mangetsu", "PERSP_MODE_MANGETSU"),
            ("video/tool/perspective/arch1t3cht", "PERSP_MODE_ARCH1T3CHT"),
        ], modes)
        for command in ("mangetsu", "distort", "arch1t3cht"):
            self.assertIn('CMD_NAME("video/tool/perspective/' + command + '")', self.commands)
        toolbar_config = json.loads((ROOT / "src/libresrc/default_toolbar.json").read_text())
        self.assertIn("video/tool/perspective", toolbar_config["visual_tools"])
        self.assertIn('STR_DISP("Perspective")', self.commands)

    def test_old_distort_shortcuts_still_select_mangetsu_mode(self):
        for command in ("video/tool/distort", "video/tool/perspective/distort"):
            self.assertIn('CMD_NAME("' + command + '")', self.commands)
        for method in ("InnerToText", "SaveOuterToLines", "UpdateInner", "UpdateOuter"):
            self.method(method)

    def test_arch_controls_are_gated_by_arch_mode(self):
        body = self.method("SetSubTool")
        gate = re.search(r"bool\s+(\w+)\s*=\s*\w+\s*==\s*PERSP_MODE_ARCH1T3CHT", body)
        self.assertIsNotNone(gate)
        for setting in ("PERSP_OUTER", "PERSP_LOCK_OUTER", "PERSP_GRID", "PERSP_ORGMODE"):
            enabled = re.search(r"EnableTool\(BUTTON_ID_BASE\s*\+\s*" + setting + r",\s*([^;]+)\);", body)
            self.assertIsNotNone(enabled, setting)
            self.assertIn(gate.group(1), enabled.group(1), setting)
        lock = re.search(r"EnableTool\(BUTTON_ID_BASE\s*\+\s*PERSP_LOCK_OUTER,([^;]+)\);", body)
        self.assertIn("PERSP_OUTER", lock.group(1))
        self.assertIn("PERSP_MODE_ARCH1T3CHT", self.method("HasOuter"))

    def test_config_keeps_arch_settings_and_defaults_to_mangetsu(self):
        for path in ("src/libresrc/default_config.json", "src/libresrc/osx/default_config.json"):
            # The full Aegisub config permits trailing commas. This flat settings
            # block is standard JSON, so inspect it without imposing Python's
            # stricter parser on unrelated defaults.
            block = re.search(r'"Perspective"\s*:\s*(\{[^{}]*\})',
                              (ROOT / path).read_text(encoding="utf-8"))
            self.assertIsNotNone(block, path)
            config = json.loads(block.group(1))
            self.assertEqual(1 << 8, config["Mode"])
            for key in ("Outer", "Outer Locked", "Grid", "Org Mode"):
                self.assertIn(key, config)
        self.assertIn("mode = PERSP_MODE_MANGETSU;", self.method("SetSubTool"))

    def test_mangetsu_mode_has_four_independent_corners(self):
        mangetsu = self.method("MakeFeatures").split("DoRefresh();", 1)[0]
        self.assertNotIn("FEATURE_CENTER", mangetsu)
        self.assertIn("FEATURE_INNER", mangetsu)
        self.assertRegex(mangetsu, r"for\s*\([^;]+;\s*\w+\s*<\s*4\s*;")
        drag = self.method("DragMangetsuCorner")
        self.assertIn("feature->index", drag)
        self.assertIn("SetMangetsuPerspective", drag)

    def test_overlay_uses_the_geometry_from_the_preview_renderer(self):
        body = self.method("TextToMangetsuPerspective")
        self.assertIn("parent->GetPerspectiveLayout(active_line->Id)", body)
        self.assertIn("layout->rect", body)
        self.assertIn("layout->corners", body)
        self.assertNotIn("GetLineBaseExtents", body)
        self.assertRegex(self.header, r"OnRenderedFrame\(\).*IsMangetsuQuadMode\(\).*DoRefresh\(\)")
        provider = (ROOT / "src/subtitles_provider_libassmod.cpp").read_text()
        self.assertIn('LoadOptionalSymbol(backend, "ass_get_perspective_layout"', provider)
        self.assertIn("frame.perspective_layouts.push_back(layout)", provider)
        worker = (ROOT / "src/async_video_provider.cpp").read_text(encoding="utf-8")
        self.assertIn("new AssFile(*new_subs, /*preserve_dialogue_ids=*/true)", worker)
        self.assertIn("new AssDialogue(static_cast<AssDialogueBase const&>(*changed))", worker)
        snapshot = (ROOT / "src/ass_file.cpp").read_text(encoding="utf-8")
        self.assertIn("preserve_dialogue_ids ? new AssDialogue(static_cast<AssDialogueBase const&>(e))", snapshot)

    def test_arch_assets_exist_in_both_themes_at_all_toolbar_sizes(self):
        manifest = (ROOT / "src/bitmaps/manifest.respack").read_text().splitlines()
        for icon in ("visual_perspective", "visual_perspective_plane", "visual_perspective_grid",
                     "visual_perspective_lock_outer", "visual_perspective_orgmode_center",
                     "visual_perspective_orgmode_nofax", "visual_perspective_orgmode_keep"):
            for theme in ("button", "button_dark"):
                for size in (16, 24, 32, 48, 64):
                    asset = f"{theme}/{icon}_{size}.png"
                    self.assertEqual(1, manifest.count(asset), asset)
                    self.assertTrue((ROOT / "src/bitmaps" / asset).is_file(), asset)


if __name__ == "__main__":
    unittest.main()
