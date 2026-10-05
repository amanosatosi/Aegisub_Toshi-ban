"""CI contracts for the native tracker/main-video boundary (no GUI startup)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, name):
    start = source.index("void DialogMotionTrack::" + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[body:end]


class NativeMotionContracts(unittest.TestCase):
    def setUp(self):
        self.window = (ROOT / "src/dialog_motion_track.cpp").read_text(encoding="utf-8")

    def test_apply_reads_main_reference_after_advanced_dialog(self):
        apply = method(self.window, "ApplyMotion")
        self.assertLess(apply.index("AdvancedMotionApply"), apply.index("int reference = context->videoController->GetFrameN()"))
        self.assertIn("MainTrack(),ClipTrack()", apply)
        self.assertEqual(apply.count("->Commit("), 1)
        self.assertIn("SetSelectionAndActive", apply)
        self.assertLess(apply.index("SetSelectionAndActive"), apply.index("->Commit("))
        self.assertNotIn("current_frame", apply)
        self.assertNotIn("Automation", apply)

    def test_hide_restore_never_owns_a_reference_or_seeks(self):
        create = method(self.window, "CreateControls")
        self.assertIn("StopPlayback(); Hide();", create)
        self.assertIn("Iconize(false); Hide();", create)
        self.assertNotIn("videoController->Jump", create)
        self.assertNotIn("ClearData()", create[create.index("minimize->Bind"):create.index("auto bottom")])
        manager = (ROOT / "src/dialog_manager.h").read_text(encoding="utf-8")
        self.assertIn("diag.second->Show();", manager)
        switch = method(self.window, "SwitchTrack")
        self.assertIn("swap(segments,other_channel.segments)", switch)
        self.assertIn("swap(result,other_channel.result)", switch)
        self.assertNotIn("videoController->Jump", switch)

    def test_native_layers_and_tests_are_registered(self):
        for filename in ("motion_track_optimizer.cpp", "motion_track_apply.cpp", "motion_track_commit.cpp", "ass_file_extradata.cpp"):
            self.assertIn(filename, (ROOT / "src/meson.build").read_text())
            self.assertIn(filename, (ROOT / "tests/meson.build").read_text())
        self.assertIn("tests/motion_track_apply.cpp", (ROOT / "tests/meson.build").read_text())
        apply = (ROOT / "src/motion_tracking/motion_track_apply.cpp").read_text()
        self.assertIn("source.ParseTags()", apply)
        self.assertNotIn('"\\\\distort', apply)
        self.assertNotIn('"\\\\perspective', apply)

    def test_revert_does_not_rebind_a_different_familys_track(self):
        revert = method(self.window, "RevertMotion")
        self.assertIn("!applied_event_ids.count(line->Id)", revert)
        self.assertIn("if (own_family) CaptureSources();", revert)
        self.assertIn("else NewSession();", revert)
        self.assertIn("!context_invalid", revert)


if __name__ == "__main__":
    unittest.main()
