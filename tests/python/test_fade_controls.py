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

    def fade_submenu(self):
        context_menu = self.method("OnSubsReadoutContextMenu")
        child = re.search(r"(?:auto|wxMenu\s*\*)\s+(\w+)\s*=\s*new wxMenu\b", context_menu)
        self.assertIsNotNone(child, "Fade choices need a native child wxMenu")
        child_name = re.escape(child.group(1))
        parent = re.search(r"(?:auto|wxMenuItem\s*\*)\s+(\w+)\s*=\s*\w+\.AppendSubMenu\(\s*"
                           + child_name + r",\s*(.*?)\);", context_menu, re.S)
        self.assertIsNotNone(parent, "The root menu must own the fade submenu")
        choices = list(re.finditer(r"(?:auto|wxMenuItem\s*\*)\s+(\w+)\s*=\s*" + child_name
                                  + r"->Append\(\s*wxID_ANY,\s*(.*?)\);", context_menu, re.S))
        return context_menu, child_name, parent, choices

    def test_fade_parent_is_only_a_native_submenu_opener(self):
        context_menu, _, parent, _ = self.fade_submenu()
        self.assertRegex(parent.group(2), r"\w+ == SubsReadoutKind::Start\s*\?\s*"
                         r'_\("Fade in from here"\)\s*:\s*_\("Fade out from here"\)')
        self.assertNotRegex(context_menu, re.escape(parent.group(1)) + r"->GetId\(\)")
        self.assertRegex(context_menu, re.escape(parent.group(1)) + r"->Enable\(fade_available\)")

    def test_directional_submenu_labels_and_ascii_custom_ellipses(self):
        context_menu, child, _, choices = self.fade_submenu()
        expected = [("Normal",), ("From white", "To white"),
                    ("From black", "To black"), ("From custom color...", "To custom color...")]
        self.assertEqual(len(expected), len(choices))
        for choice, labels in zip(choices, expected):
            self.assertEqual(list(labels), re.findall(r'_\("([^"]*)"\)', choice.group(2)))
            if len(labels) == 2:
                self.assertRegex(choice.group(2), r"\w+ == SubsReadoutKind::Start\s*\?")
            for label in labels:
                self.assertTrue(label.isascii())
        self.assertNotIn("\u2026", context_menu)
        separator = re.search(child + r"->AppendSeparator\(\s*\)", context_menu)
        self.assertIsNotNone(separator)
        self.assertLess(choices[0].end(), separator.start())
        self.assertLess(separator.end(), choices[1].start())

    def test_submenu_entries_keep_their_fade_color_actions(self):
        context_menu, _, _, choices = self.fade_submenu()
        self.assertEqual(4, len(choices))
        for item, action in zip(choices, ("Normal", "White", "Black", "Pick")):
            self.assertRegex(context_menu, r"\w+\.GetId\(\)\s*==\s*" + re.escape(item.group(1))
                             + r"->GetId\(\)\)\s*SetFadeFromHere\(\w+,\s*\w+,\s*"
                             + rf"FadeColorChoice::{action}\)")

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
