"""UI wiring contracts complement the executable fade operation regressions in CI."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FadeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/video_box.cpp").read_text(encoding="utf-8-sig")
        cls.header = (ROOT / "src/video_box.h").read_text(encoding="utf-8-sig")
        cls.buttons = cls.source.split("void VideoBox::MakeFadeButtons", 1)[1].split(
            "bool VideoBox::SetFadeFromHere", 1)[0]
        cls.operation = cls.source.split("bool VideoBox::SetFadeFromHere", 1)[1].split(
            "bool VideoBox::HandleReadoutClick", 1)[0]

    def test_both_controls_are_visible_native_button_pairs(self):
        for side, label in (("Start", "Fade in from here"), ("End", "Fade out from here")):
            self.assertIn(f'SubsReadoutKind::{side}, _("{label}")', self.source)
        self.assertEqual(2, self.buttons.count("new wxButton"))
        self.assertIn("VideoSizer->Add(fadeSizer", self.source)
        self.assertNotIn("wxEVT_RIGHT", self.buttons)
        self.assertNotIn("wxEVT_CONTEXT_MENU", self.buttons)

    def test_main_click_uses_normal_choice(self):
        primary = self.buttons.split("primary->Bind", 1)[1].split("dropdown->Bind", 1)[0]
        self.assertIn("SetFadeFromHere(kind, GetFadeDuration(kind));", primary)
        self.assertNotIn("Pick", primary)
        self.assertIn("choice = agi::ass::FadeColorChoice::Normal", self.header)

    def test_visible_menu_routes_each_color_choice(self):
        for label, choice in (("Color white", "White"), ("Color black", "Black"), ("Pick color…", "Pick")):
            self.assertIn(f'_("{label}")', self.buttons)
            self.assertIn(f"SetFadeFromHere(kind, milliseconds, FadeColorChoice::{choice})", self.buttons)
        self.assertIn("dropdown->PopupMenu", self.buttons)

    def test_picker_and_commit_are_outside_selection_loop(self):
        picker = self.operation.split("auto pick_color =", 1)[1].split("auto apply_selection =", 1)[0]
        apply = self.operation.split("auto apply_selection =", 1)[1].split("auto commit =", 1)[0]
        commit = self.operation.split("auto commit =", 1)[1].split("if (!agi::ass::RunFadeOperation", 1)[0]
        self.assertEqual(1, picker.count("GetColorFromUser("))
        self.assertNotIn("GetColorFromUser", apply)
        self.assertNotIn("->Commit(", apply)
        self.assertEqual(1, commit.count("->Commit("))
        self.assertIn("RunFadeOperation(choice, pick_color, apply_selection, commit)", self.operation)
        self.assertIn('COMMIT_DIAG_TEXT, -1, commit_line', commit)

    def test_selection_reuses_duration_calculated_from_active_line(self):
        self.assertIn("active->Start, active->End", self.source)
        self.assertIn("line->Text.get(), side, milliseconds, color", self.operation)
        self.assertNotIn("FadeDurationFromVideoTime", self.operation)
        self.assertNotIn("line->Start", self.operation)
        self.assertNotIn("line->End", self.operation)


if __name__ == "__main__":
    unittest.main()
