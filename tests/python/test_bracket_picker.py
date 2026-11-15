"""The keyboard picker is a command with an opt-in shortcut."""
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BracketPickerContract(unittest.TestCase):
    def test_command_is_registered_and_uses_the_existing_editor(self):
        source = (ROOT / "src/command/edit.cpp").read_text(encoding="utf-8")
        command = source.split("struct edit_bracket final", 1)[1].split("struct ", 1)[0]
        self.assertIn('CMD_NAME("edit/bracket")', command)
        self.assertIn("c->subsEditBox->ShowBracketPicker()", command)
        self.assertIn("reg(agi::make_unique<edit_bracket>())", source)
        preferences = (ROOT / "src/preferences.cpp").read_text(encoding="utf-8")
        self.assertIn("autocomplete(to_wx(cmd::get_registered_commands()))", preferences)

    def test_command_is_opt_in_on_each_platform(self):
        resources = ROOT / "src/libresrc"
        for path in resources.rglob("default_hotkey.json"):
            contexts = json.loads(path.read_text(encoding="utf-8"))
            self.assertIn("Subtitle Edit Box", contexts)
            for commands in contexts.values():
                self.assertNotIn("edit/bracket", commands, str(path))
        for path in (resources / "default_menu.json", resources / "default_toolbar.json",
                     ROOT / "src/hotkey.cpp"):
            self.assertNotIn("edit/bracket", path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
