"""Source contracts for release packaging; run in GitHub Actions."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class ReleasePreparation(unittest.TestCase):
    def test_dependencycontrol_pin_and_retired_patch(self):
        setup = read("tools/win-installer-setup.ps1")
        self.assertIn('$DependencyControlVersion = "v0.9.0"', setup)
        self.assertIn("checkout --force $DependencyControlVersion", setup)
        self.assertFalse((ROOT / "tools/patches/dependencycontrol/0001-windows-unicode-long-paths.patch").exists())
        for path in ("tools/win-installer-setup.ps1", "packages/win_installer/portable/create-portable.ps1"):
            source = read(path)
            self.assertNotIn("Apply-GitPatch", source)
            self.assertNotIn("ffi-experiments", source)

    def test_both_packagers_copy_new_namespace_and_toolbox(self):
        installer = read("packages/win_installer/fragment_automation.iss")
        portable = read("packages/win_installer/portable/create-portable.ps1")
        for source in (installer, portable):
            self.assertIn("DependencyControl\\modules\\l0\\*", source)
            self.assertIn("automation\\include\\l0", source)
            self.assertIn("DependencyControl\\macros\\*", source)
            self.assertNotIn("DependencyControl\\modules\\*", source)
            self.assertNotIn("ffi-experiments", source)
        self.assertFalse((ROOT / "automation/autoload/garret.depctrl_config.lua").exists())
        self.assertNotIn("garret.depctrl_config.lua", read("automation/meson.build"))
        self.assertFalse(any("garret.depctrl_config.lua" in line for line in installer.splitlines()
                             if line.startswith("DestDir:")))

    def test_cleanup_removes_code_and_preserves_state(self):
        source = read("packages/win_installer/fragment_automation.iss")
        deletions = source.split("[InstallDelete]", 1)[1]
        paths = re.findall(r'Name: "([^"]+)"', deletions)
        for suffix in (r"include\l0\DependencyControl.moon", r"include\l0\DependencyControl",
                       r"autoload\l0.DependencyControl.Toolbox.moon", r"autoload\l0.DependencyControl.Toolbox"):
            self.assertIn("{userappdata}\\Aegisub\\automation\\" + suffix, paths)
        for path in paths:
            self.assertIn("\\automation\\", path)
            self.assertNotIn("*", path, "Cleanup must target owned payloads precisely")
            self.assertNotIn(".json", path)
        bridge = next(line for line in source.splitlines() if "DestName: l0.DependencyControl.json" in line)
        for flag in ("external", "onlyifdoesntexist", "skipifsourcedoesntexist", "uninsneveruninstall"):
            self.assertIn(flag, bridge)

    def test_runtime_build_contract(self):
        moonscript = read("automation/include/moonscript.lua")
        version = re.search(r'local version = "(\d+)\.(\d+)\.(\d+)"', moonscript)
        self.assertIsNotNone(version)
        self.assertGreaterEqual(tuple(map(int, version.groups())), (0, 3, 0))
        self.assertIn("-DLUAJIT_ENABLE_LUA52COMPAT", read("subprojects/packagefiles/luajit/meson.build"))
        self.assertIn("System luajit found but not compiled in 5.2 mode", read("meson.build"))
        self.assertIn("__pairs", read("tests/tests/dependencycontrol_runtime.cpp"))

    def test_offer_is_optional_and_precedes_projects(self):
        main = read("src/main.cpp")
        self.assertLess(main.index("RegisterOptional(MangetsuRendererOfferKey)"), main.index("config::opt->ConfigUser()"))
        self.assertLess(main.index("HandleMangetsuRendererOffer("), main.index("NewProjectContext();"))
        self.assertNotIn("Mangetsu Renderer Offer Seen", read("src/libresrc/default_config.json"))
        self.assertNotIn("Mangetsu Renderer Offer Seen", read("packages/win_installer/portable/config.json"))

    def test_updater_has_one_repository_and_no_legacy_service(self):
        source = read("src/dialog_version_check.cpp")
        self.assertIn("FindApplicationUpdates(DownloadApplicationUpdateJson, GetGitHash()", source)
        self.assertNotIn("UPDATE_CHECKER_SERVER", source)
        self.assertNotIn("updates.aegisub.org", read("meson_options.txt"))
        self.assertIn('ApplicationUpdateRepository = "amanosatosi/Aegisub_Toshi-ban"', read("src/application_updates.h"))


if __name__ == "__main__":
    unittest.main()
