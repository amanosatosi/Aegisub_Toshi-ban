"""wx readout wiring contracts; timing, color and undo behavior is tested in C++."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FadeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/video_box.cpp").read_text(encoding="utf-8-sig")
        cls.constructor = cls.source.split("VideoBox::VideoBox", 1)[1].split("void VideoBox::", 1)[0]

    def method(self, name):
        # Extract one wx handler/helper, allowing formatting and local names to change.
        match = re.search(r"(?:void|bool) VideoBox::" + name + r"\([^)]*\)[^{]*\{"
                          r"(.*?)(?=\n(?:void|bool) VideoBox::|\Z)", self.source, re.S)
        self.assertIsNotNone(match, name)
        return match.group(1)

    def test_no_permanent_fade_controls_or_extra_video_row(self):
        self.assertNotIn("Fade in from here", self.constructor)
        self.assertNotIn("Fade out from here", self.constructor)
        self.assertNotRegex(self.source, r"new wx(?:Bitmap)?Button\b")
        # The existing layout is video/tools, separator, slider, and control row.
        self.assertEqual(4, len(re.findall(r"VideoSizer->Add\(", self.constructor)))
        self.assertNotRegex(self.constructor, r"VideoSizer->AddSpacer|VideoSizer->AddStretchSpacer")

    def test_right_click_is_bound_only_to_subtitle_relative_readout(self):
        bindings = re.findall(r"(\w+)->Bind\(wxEVT_CONTEXT_MENU,\s*&VideoBox::OnSubsReadoutContextMenu",
                              self.source)
        self.assertEqual(["VideoSubsPos"], bindings)
        context_menu = self.method("OnSubsReadoutContextMenu")
        self.assertRegex(context_menu, r"GetSubsReadoutForPosition\(position,\s*\w+,\s*&\w+\)")
        self.assertIn("VideoSubsPos->PopupMenu", context_menu)

    def test_numeric_region_selects_the_corresponding_fade_direction(self):
        hit_test = self.method("GetSubsReadoutForPosition")
        self.assertRegex(hit_test, r"value = subs_offset_readout_;\s*if \(kind\) \*kind = SubsReadoutKind::Start")
        self.assertRegex(hit_test, r"value = subs_remaining_readout_;\s*if \(kind\) \*kind = SubsReadoutKind::End")
        context_menu = self.method("OnSubsReadoutContextMenu")
        self.assertRegex(context_menu, r"kind == SubsReadoutKind::Start\s*\?\s*"
                         r'_\("Fade in from here"\)\s*:\s*_\("Fade out from here"\)')
        for label, choice in ((None, "Normal"), ("Color white", "White"),
                              ("Color black", "Black"), ("Pick color…", "Pick")):
            if label:
                self.assertIn(f'_("{label}")', context_menu)
            self.assertRegex(context_menu, rf"SetFadeFromHere\(kind,\s*video_time,\s*FadeColorChoice::{choice}\)")

    def test_left_click_keeps_existing_readout_action_without_fade(self):
        self.assertRegex(self.source, r"VideoSubsPos->Bind\(wxEVT_LEFT_DOWN,\s*&VideoBox::OnSubsReadoutClick")
        left_click = self.method("OnSubsReadoutClick")
        self.assertIn("GetSubsReadoutForPosition", left_click)
        self.assertIn("HandleReadoutClick(value)", left_click)
        for method in (left_click, self.method("HandleReadoutClick")):
            self.assertNotIn("SetFadeFromHere", method)
            self.assertNotIn("PopupMenu", method)
            self.assertNotIn("GetColorFromUser", method)
        readout_action = self.method("HandleReadoutClick")
        self.assertIn('OPT_GET("Video/Click Time Readout Action")', readout_action)
        self.assertIn("CopyReadoutToClipboard", readout_action)
        self.assertIn("InsertReadoutIntoEditBox", readout_action)


if __name__ == "__main__":
    unittest.main()
