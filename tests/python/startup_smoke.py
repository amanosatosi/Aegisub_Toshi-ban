"""Exercise the packaged Windows GUI in CI, using an isolated portable profile.

No testing hooks are compiled into the product. Timings and logs are retained
under --output; these are observations, not performance improvement claims.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def run(portable, output):
    user = C.WinDLL("user32", use_last_error=True)
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    for name, args, result in (
        ("EnumWindows", [callback, W.LPARAM], W.BOOL),
        ("EnumChildWindows", [W.HWND, callback, W.LPARAM], W.BOOL),
        ("GetWindowThreadProcessId", [W.HWND, C.POINTER(W.DWORD)], W.DWORD),
        ("GetWindowTextW", [W.HWND, W.LPWSTR, C.c_int], C.c_int),
        ("GetClassNameW", [W.HWND, W.LPWSTR, C.c_int], C.c_int),
        ("GetMenu", [W.HWND], W.HMENU),
        ("GetSubMenu", [W.HMENU, C.c_int], W.HMENU),
        ("GetMenuItemCount", [W.HMENU], C.c_int),
        ("GetMenuItemID", [W.HMENU, C.c_int], W.UINT),
        ("GetMenuStringW", [W.HMENU, W.UINT, W.LPWSTR, C.c_int, W.UINT], C.c_int),
        ("PostMessageW", [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL),
        ("SendMessageTimeoutW", [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)], W.LPARAM),
        ("IsWindowVisible", [W.HWND], W.BOOL),
    ):
        fn = getattr(user, name)
        fn.argtypes, fn.restype = args, result

    def text(hwnd, cls=False):
        buf = C.create_unicode_buffer(2048)
        (user.GetClassNameW if cls else user.GetWindowTextW)(hwnd, buf, len(buf))
        return buf.value

    def windows(pid, parent=None):
        found = []

        @callback
        def collect(hwnd, _):
            owner = W.DWORD()
            user.GetWindowThreadProcessId(hwnd, C.byref(owner))
            if owner.value == pid:
                found.append(hwnd)
            return True

        if parent:
            user.EnumChildWindows(parent, collect, 0)
        else:
            user.EnumWindows(collect, 0)
        return found

    def wait(predicate, description, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.05)
        raise AssertionError("Timed out: " + description)

    def send(hwnd, message, wparam=0, lparam=0):
        result = C.c_size_t()
        assert user.SendMessageTimeoutW(hwnd, message, wparam, lparam, 2, 1000, C.byref(result)), "GUI stopped responding"
        return result.value

    def entries(menu):
        result = []
        for index in range(user.GetMenuItemCount(menu)):
            label = C.create_unicode_buffer(2048)
            user.GetMenuStringW(menu, index, label, len(label), 0x400)
            result.append((label.value.replace("&", "").split("\t")[0], user.GetMenuItemID(menu, index)))
            child = user.GetSubMenu(menu, index)
            if child:
                result.extend(entries(child))
        return result

    def invoke(frame, label):
        choices = [item for item in entries(user.GetMenu(frame)) if item[0] == label]
        assert len(choices) == 1, (label, choices)
        assert user.PostMessageW(frame, 0x111, choices[0][1], 0)

    measurements = []
    for scenario in ("empty", "scripts", "cancel", "Mangetsu", "libassmod", "missing"):
        profile = output / scenario
        profile.mkdir(parents=True, exist_ok=True)
        for path in portable.iterdir():
            if path.suffix.lower() in (".exe", ".dll"):
                shutil.copy2(path, profile / path.name)
        shutil.copytree(portable / "automation" / "include", profile / "automation" / "include", dirs_exist_ok=True)
        autoload = profile / "autoload"
        autoload.mkdir(exist_ok=True)
        selected = scenario if scenario in ("Mangetsu", "libassmod") else "libass"
        if scenario == "missing":
            selected = "Mangetsu"
            (profile / "mangetsu.dll").unlink()
        config = {
            "App": {"First Start": False, "Language": "en_US", "Dark Mode": scenario == "cancel",
                    "Auto": {"Check For Updates": False}, "Mangetsu Renderer Offer Seen": True},
            "Subtitle": {"Provider": selected},
            "Video": {"Script Resolution Mismatch": 0},
            "Path": {"Automation": {"Autoload": str(autoload) + "|" + str(autoload),
                                    "Include": str(profile / "automation" / "include")}},
        }
        (profile / "config.json").write_text(json.dumps(config), encoding="utf-8")
        if scenario == "scripts":
            (autoload / "a.lua").write_text("""
local deadline = os.clock() + 2
while os.clock() < deadline do end
require('aegisub.clipboard').get()
aegisub.register_macro('Smoke Lua', '', function()
    local f = assert(io.open(os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/lua-ran', 'w'))
    f:write('ok'); f:close()
end)
""", encoding="utf-8")
            (autoload / "b.moon").write_text("""
aegisub.register_macro 'Smoke Moon', '', ->
  f = assert io.open os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/moon-ran', 'w'
  f\\write 'ok'
  f\\close!
""", encoding="utf-8")
            (autoload / "c.lua").write_text("aegisub.register_macro('Must not publish', '', function() end); error('expected invalid script')", encoding="utf-8")
            (autoload / "ignored.txt").write_text("error('unsupported extension')", encoding="utf-8")
        elif scenario == "cancel":
            (autoload / "infinite.lua").write_text("while true do end", encoding="utf-8")
        env = dict(os.environ, CI="true", AEGISUB_STARTUP_SMOKE_DIR=str(profile))
        started = time.monotonic()
        process = subprocess.Popen([str(profile / "aegisub.exe")], cwd=profile, env=env)
        try:
            frame = wait(lambda: next((h for h in windows(process.pid) if user.GetMenu(h)), None), "main frame")
            assert user.IsWindowVisible(frame), "Main frame must be visible"
            send(frame, 0)
            visible_ms = (time.monotonic() - started) * 1000

            def logs():
                return "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in (profile / "log").glob("*.json"))

            if scenario == "scripts":
                wait(lambda: any(text(h) == "Loading Automation scripts" and user.IsWindowVisible(h) for h in windows(process.pid, frame)), "progress before completion")
                # Editing works during initialization, without moving keyboard focus.
                edit = wait(lambda: next((h for h in windows(process.pid, frame) if text(h, True) == "Scintilla" and user.IsWindowVisible(h)), None), "subtitle editor")
                length = send(edit, 2183)  # SCI_GETTEXTLENGTH
                send(edit, 0x102, ord("x"))  # WM_CHAR; no cross-process pointer.
                assert send(edit, 2183) == length + 1
                send(frame, 0)
                wait(lambda: "3 / 3" in logs(), "three scripts completed, including the invalid script")
                assert "failures: 1" in logs()
                assert "unsupported extension" not in logs()
                assert not any(name == "Must not publish" for name, _ in entries(user.GetMenu(frame)))
                for label, marker in (("Smoke Lua", "lua-ran"), ("Smoke Moon", "moon-ran")):
                    invoke(frame, label)
                    wait(lambda: (profile / marker).exists(), label + " execution")
                invoke(frame, "Automation...")
                manager = wait(lambda: next((h for h in windows(process.pid) if text(h) == "Automation Manager"), None), "Automation Manager")
                rescan = wait(lambda: next((h for h in windows(process.pid, manager) if "scan Autoload Dir" in text(h)), None), "Rescan button")
                for _ in range(3):
                    user.PostMessageW(rescan, 0xF5, 0, 0)  # BM_CLICK, repeated rescans coalesce.
                    time.sleep(0.05)
                wait(lambda: any(text(h) == "Loading Automation scripts" and user.IsWindowVisible(h) for h in windows(process.pid, frame)), "manual progress")
                wait(lambda: logs().count('"section" : "startup/automation"') >= 2 or len(re.findall(r'"section"\s*:\s*"startup/automation"', logs())) >= 2, "rescan completion")
                user.PostMessageW(manager, 0x10, 0, 0)
                for label in ("Smoke Lua", "Smoke Moon"):
                    assert sum(name == label for name, _ in entries(user.GetMenu(frame))) == 1
                # Avoid a save prompt after the intentional edit; undo via SCI_UNDO.
                send(edit, 2176)
            elif scenario == "cancel":
                wait(lambda: "infinite.lua" in " ".join(text(h) for h in windows(process.pid, frame)), "infinite script started")
                send(frame, 0)
            else:
                wait(lambda: "startup/automation" in logs(), "empty autoload completion")
                assert not any(text(h, True).lower() == "msctls_progress32" and user.IsWindowVisible(h) for h in windows(process.pid, frame)), "No progress bar for empty directories"
                invoke(frame, "Use Dummy Video...")
                dialog = wait(lambda: next((h for h in windows(process.pid) if "dummy video options" in text(h).lower()), None), "dummy video dialog")
                ok = wait(lambda: next((h for h in windows(process.pid, dialog) if text(h).replace("&", "") == "OK"), None), "dummy video OK")
                user.PostMessageW(ok, 0xF5, 0, 0)
                if scenario == "missing":
                    warning = wait(lambda: next((h for h in windows(process.pid) if h != frame and "fallback" in " ".join(text(c) for c in windows(process.pid, h)).lower()), None), "missing renderer warning")
                    user.PostMessageW(warning, 0x10, 0, 0)
                wait(lambda: "startup/renderer" in logs(), "selected renderer creation")
                if scenario in ("empty", "missing"):
                    assert "libassmod loaded from" not in logs()
                    assert "Mangetsu loaded from" not in logs()
                elif scenario == "Mangetsu":
                    assert "Mangetsu loaded from" in logs() and "libassmod loaded from" not in logs()
                else:
                    assert "libassmod loaded from" in logs() and "Mangetsu loaded from" not in logs()

            user.PostMessageW(frame, 0x10, 0, 0)
            # An edit/save prompt, if a platform's Scintilla undo semantics differ.
            deadline = time.monotonic() + 10
            while process.poll() is None and time.monotonic() < deadline:
                for top in windows(process.pid):
                    for child in windows(process.pid, top):
                        if text(child).replace("&", "") == "No":
                            user.PostMessageW(child, 0xF5, 0, 0)
                time.sleep(0.05)
            assert process.poll() == 0, "Shutdown crashed or hung"
            measurements.append({"scenario": scenario, "observed_visible_and_responsive_ms": visible_ms})
            assert "First responsive idle event" in logs()
            if scenario == "missing":
                assert json.loads((profile / "config.json").read_text(encoding="utf-8-sig"))["Subtitle"]["Provider"] == "Mangetsu"
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    (output / "measurements.json").write_text(json.dumps(measurements, indent=2), encoding="utf-8")
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--portable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.portable.resolve(), args.output.resolve())
