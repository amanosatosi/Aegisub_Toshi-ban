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
        self.assertIsNotNone(child, "Color choices need a native child wxMenu")
        child_name = re.escape(child.group(1))
        parent = re.search(r"(?:auto|wxMenuItem\s*\*)\s+(\w+)\s*=\s*\w+\.AppendSubMenu\(\s*"
                           + child_name + r",\s*(.*?)\);", context_menu, re.S)
        self.assertIsNotNone(parent, "The root menu must own the color submenu")
        choices = list(re.finditer(r"(?:auto|wxMenuItem\s*\*)\s+(\w+)\s*=\s*" + child_name
                                  + r"->Append\(\s*wxID_ANY,\s*(.*?)\);", context_menu, re.S))
        return context_menu, child_name, parent, choices

    def test_direct_fade_is_a_normal_command_and_submenu_holds_other_options(self):
        context_menu, _, parent, choices = self.fade_submenu()
        direct = re.search(r"auto\s+(\w+)\s*=\s*menu\.Append\(wxID_ANY,\s*"
                           r"(kind == SubsReadoutKind::Start\s*\?\s*"
                           r'_\("Fade in from here"\)\s*:\s*_\("Fade out from here"\))\);',
                           context_menu, re.S)
        self.assertIsNotNone(direct, "Fade in/out must be a directly clickable root action")
        direct_name = direct.group(1)
        self.assertIn(direct_name + "->Enable(fade_available)", context_menu)
        self.assertRegex(parent.group(2), r'_\("Other fade options"\)')
        self.assertIn(parent.group(1) + "->Enable(fade_available)", context_menu)
        self.assertNotIn(parent.group(1) + "->GetId()", context_menu)
        self.assertRegex(context_menu,
                         r"command.GetId\(\) == " + re.escape(direct_name) +
                         r"->GetId\(\)\)\s*SetFadeFromHere\(kind, video_time, FadeColorChoice::Normal\)")
        self.assertLess(context_menu.index("menu.AppendSeparator()"),
                        context_menu.index("auto " + direct_name))
        self.assertEqual(3, len(choices))

    def test_directional_color_labels_and_dispatch(self):
        context_menu, _, _, choices = self.fade_submenu()
        expected = (("From white", "To white", "White"),
                    ("From black", "To black", "Black"),
                    ("From custom color...", "To custom color...", "Pick"))
        self.assertEqual(3, len(choices))
        for choice, (in_label, out_label, action) in zip(choices, expected):
            self.assertEqual([in_label, out_label], re.findall(r'_\("([^"]*)"\)', choice.group(2)))
            self.assertRegex(choice.group(2), r"kind == SubsReadoutKind::Start\s*\?")
            self.assertRegex(context_menu, r"command.GetId\(\) == " + re.escape(choice.group(1)) +
                             r"->GetId\(\)\)\s*SetFadeFromHere\(kind, video_time, FadeColorChoice::" +
                             action + r"\)")
        self.assertNotIn("\\u2026", context_menu)
        self.assertNotIn("fade_menu->AppendSeparator()", context_menu)
        self.assertNotIn('fade_menu->Append(wxID_ANY, _("Normal"))', context_menu)

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
