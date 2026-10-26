"""Protect the chat editor's toolbar entry; appearance behavior is tested in C++."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ChatStyleControls(unittest.TestCase):
    def test_toolbar_position_keeps_the_existing_spacer_and_commit_button(self):
        source = (ROOT / "src/subs_edit_box.cpp").read_text(encoding="utf-8-sig")
        calls = re.findall(r'MakeButton\("([^"]+)"\)', source)
        index = calls.index("edit/color/gradient")
        self.assertEqual(["edit/color/gradient", "edit/chat/style", "grid/line/next/create"], calls[index:index + 3])
        region = source.split('MakeButton("edit/chat/style")', 1)[1].split('MakeButton("grid/line/next/create")', 1)[0]
        self.assertIn("AddSpacer(5)", region)
        self.assertIn("wxEVT_UPDATE_UI", region)
        self.assertIn("Validate(c)", region)

    def test_command_uses_active_line_validation_and_the_dedicated_dialog(self):
        source = (ROOT / "src/command/edit.cpp").read_text(encoding="utf-8-sig")
        command = source.split('CMD_NAME("edit/chat/style")', 1)[1].split("struct ", 1)[0]
        self.assertIn("COMMAND_VALIDATE", command)
        self.assertIn("GetActiveLine()", command)
        self.assertIn("ShowMangetsuChatStyleDialog(c)", command)
        self.assertRegex(command, r"wxBitmap Icon\(")

    def test_dialog_keeps_the_existing_picker_and_preview_is_local(self):
        source = (ROOT / "src/dialog_mangetsu_chat_style.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("GetColorFromUser", source)
        self.assertNotIn("wxColourDialog", source)
        self.assertNotIn("PreviewSubtitleText", source)
        self.assertIn("wxRESIZE_BORDER", source)


if __name__ == "__main__":
    unittest.main()
