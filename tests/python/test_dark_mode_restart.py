"""Source-level regression contracts for deferred Windows dark-mode restarts.

Interactive window shutdown and relaunch are verified by Windows UI acceptance tests.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class DarkModeRestart(unittest.TestCase):
    def test_startup_tracks_applied_native_dark_mode(self):
        app = read("src/main.cpp")
        header = read("src/main.h")
        self.assertIn('dark_mode_at_startup = OPT_GET("App/Dark Mode")->GetBool();', app)
        self.assertIn("libresrc_set_dark_icons_enabled(dark_mode_at_startup);", app)
        self.assertIn("if (dark_mode_at_startup) {", app)
        self.assertIn("MSWEnableDarkMode(wxApp::DarkMode_Always)", app)
        self.assertIn("bool IsDarkModeActive() const { return dark_mode_at_startup; }", header)

    def test_apply_detects_deferred_restart_and_does_not_nag(self):
        source = read("src/preferences.cpp")
        header = read("src/preferences.h")
        self.assertIn("dark_mode_at_startup = wxGetApp().IsDarkModeActive();", source)
        self.assertIn('last_committed_dark_mode = OPT_GET("App/Dark Mode")->GetBool();', source)
        self.assertIn("dark_mode == dark_mode_at_startup || restart_prompt_dismissed", source)
        self.assertIn("restart_prompt_dismissed = true;", source)
        self.assertIn("restart_prompt_dismissed = false;", source)
        self.assertIn("bool const had_changes = !pending_changes.empty() || !pending_callbacks.empty();", source)
        self.assertIn("if (had_changes)\n\t\tPromptForDarkModeRestart();", source)
        self.assertNotIn("dark_mode_at_open", header + source)

    def test_now_and_later_use_safe_existing_restart_path(self):
        prefs = read("src/preferences.cpp")
        commands = read("src/command/app.cpp")
        main = read("src/main.cpp")
        for source in (prefs, commands):
            self.assertIn('SetYesNoLabels(_("Restart Now"), _("Later"))', source)
            self.assertIn("SetEscapeId(wxID_NO)", source)
        self.assertIn("if (preferences.ShouldRestart())", prefs)
        self.assertIn("wxGetApp().RequestRestart(c);", commands)
        self.assertIn("if (!frame->Close())", main)
        self.assertIn("restart_on_exit = true;", main)
        self.assertIn("RestartAegisub(arguments);", main)


if __name__ == "__main__":
    unittest.main()
