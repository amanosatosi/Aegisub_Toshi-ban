"""Contracts complementing the packaged Windows GUI and native Lua tests."""
from pathlib import Path
import unittest
from startup_smoke import visible_main_frame

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class StartupLoading(unittest.TestCase):
    def test_main_frame_is_not_ready_until_shown(self):
        handles = [1, 2]
        menus = {1: True, 2: False}
        visible = {1: False, 2: True}
        # The main frame has its menu but is still under construction. An
        # unrelated visible helper window must not be selected either.
        self.assertIsNone(visible_main_frame(handles, menus.get, visible.get))
        visible[1] = True
        self.assertEqual(1, visible_main_frame(handles, menus.get, visible.get))

    def test_startup_has_no_provider_enumeration_or_blocking_autoload_constructor(self):
        app = read("src/main.cpp")
        self.assertNotIn("SubtitlesProviderFactory::GetClasses()", app)
        base = read("src/auto4_base.cpp")
        constructor = base.split("AutoloadScriptManager::AutoloadScriptManager", 1)[1].split("AutoloadScriptManager::~", 1)[0]
        self.assertNotIn("Reload();", constructor)
        self.assertNotIn("std::async", base)
        self.assertIn("std::chrono::steady_clock", base)

    def test_worker_never_registers_features_and_ui_publishes_after_execution(self):
        lua = read("src/auto4_lua.cpp")
        command = lua.split("int LuaCommand::LuaRegister", 1)[1].split("LuaCommand::LuaCommand", 1)[0]
        filters = lua.split("int LuaExportFilter::LuaRegister", 1)[1].split("void LuaExportFilter::ProcessSubs", 1)[0]
        self.assertNotIn("cmd::reg", command)
        self.assertNotIn("AssExportFilterChain::Register", filters)
        self.assertIn("StageCommand", command)
        base = read("src/auto4_base.cpp")
        self.assertLess(base.index("if (!reload_pending && script_execution_count)"), base.index("script->Publish();"))
        self.assertIn("batch->context->cancelled = true", base)
        self.assertIn("worker.join()", base)
        self.assertNotIn(".detach()", base)
        self.assertIn("script_execution_guard", read("src/dialog_export.cpp"))

    def test_frame_owns_native_progress_and_disconnects_before_child_teardown(self):
        frame = read("src/frame_main.cpp")
        self.assertIn("new wxGauge", frame)
        self.assertIn("wxALIGN_RIGHT", frame)
        self.assertIn("progress.completed, progress.total", frame)
        self.assertIn("StartOnce(2000)", frame)
        destructor = frame.split("FrameMain::~FrameMain", 1)[1].split("void FrameMain::EnableToolBar", 1)[0]
        self.assertLess(destructor.index("automation_progress_connection.Disconnect()"), destructor.index("DestroyChildren()"))
        self.assertIn("all_items.clear()", read("src/menu.cpp"))


if __name__ == "__main__":
    unittest.main()
