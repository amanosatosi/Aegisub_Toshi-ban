import importlib.util
from pathlib import Path
from types import ModuleType, SimpleNamespace
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


class FakeModifiedClip:
    def __init__(self, clip, callback):
        self.clip = clip
        self.callback = callback

    def frames(self, close=True):
        del close
        for n, scene_change in enumerate(self.clip.scene_changes):
            frame = SimpleNamespace(
                props=SimpleNamespace(_SceneChangePrev=scene_change)
            )
            yield self.callback(n, frame)


class FakeStd:
    def __init__(self, clip):
        self.clip = clip

    def ModifyFrame(self, clip, callback):
        return FakeModifiedClip(clip, callback)


class FakeClip:
    def __init__(self, scene_changes):
        self.scene_changes = scene_changes
        self.width = 1920
        self.height = 1080
        self.num_frames = len(scene_changes)
        self.std = FakeStd(self)


class FakeScxvid:
    def __init__(self):
        self.calls = []

    def Scxvid(self, clip, **kwargs):
        self.calls.append(kwargs)
        return clip


class FakeResize:
    @staticmethod
    def Bilinear(clip, **kwargs):
        del kwargs
        return clip


class FakeCore:
    def __init__(self):
        self.resize = FakeResize()
        self.scxvid = FakeScxvid()

    @staticmethod
    def log_message(*args):
        del args


def load_aegisub_vs():
    fake_vs = ModuleType("vapoursynth")
    fake_vs.core = FakeCore()
    fake_vs.VideoNode = FakeClip
    fake_vs.VideoFrame = object
    fake_vs.Error = RuntimeError
    fake_vs.GRAY8 = 1
    fake_vs.MESSAGE_TYPE_DEBUG = 0

    previous = sys.modules.get("vapoursynth")
    sys.modules["vapoursynth"] = fake_vs
    try:
        module_path = REPO_ROOT / "automation" / "vapoursynth" / "aegisub_vs.py"
        spec = importlib.util.spec_from_file_location("aegisub_vs_under_test", module_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        if previous is None:
            del sys.modules["vapoursynth"]
        else:
            sys.modules["vapoursynth"] = previous


class SceneDetectorRuntimeTest(unittest.TestCase):
    def test_legacy_false_setting_falls_back_to_scxvid(self):
        module = load_aegisub_vs()
        clip = FakeClip([False, True, False, True])

        keyframes = module.make_keyframes(
            clip, use_scxvid=False, threshold=0.42
        )

        self.assertEqual([1, 3], keyframes)
        self.assertEqual([{"threshold": 0.42}], module.core.scxvid.calls)

    def test_scxvid_is_the_default_backend(self):
        module = load_aegisub_vs()
        clip = FakeClip([True, False])

        keyframes = module.make_keyframes(clip)

        self.assertEqual([0], keyframes)
        self.assertEqual([{}], module.core.scxvid.calls)


class WindowsPackagingTest(unittest.TestCase):
    def test_obsolete_wwxd_dependency_is_absent(self):
        paths = [
            REPO_ROOT / "tools" / "win-installer-setup.ps1",
            REPO_ROOT / "packages" / "win_installer" / "fragment_codecs.iss",
            REPO_ROOT / "packages" / "win_installer" / "portable" / "create-portable.ps1",
        ]
        for path in paths:
            with self.subTest(path=path):
                self.assertNotIn("wwxd", path.read_text(encoding="utf-8").lower())

    def test_scxvid_remains_in_installer_and_portable_packages(self):
        installer = (
            REPO_ROOT / "packages" / "win_installer" / "fragment_codecs.iss"
        ).read_text(encoding="utf-8")
        portable = (
            REPO_ROOT
            / "packages"
            / "win_installer"
            / "portable"
            / "create-portable.ps1"
        ).read_text(encoding="utf-8")

        self.assertIn(r"SCXVid\libscxvid.dll", installer)
        self.assertIn(r"SCXVid\libscxvid.dll", portable)


if __name__ == "__main__":
    unittest.main()
