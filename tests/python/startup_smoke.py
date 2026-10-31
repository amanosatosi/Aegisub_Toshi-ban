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


def visible_main_frame(handles, get_menu, is_visible):
    # A frame gets its menu before its constructor calls Show(). A menu alone
    # identifies a candidate, not a window that is ready for interaction.
    return next((h for h in handles if get_menu(h) and is_visible(h)), None)


def visible_warning_dialog(handles, get_title, get_class, is_visible):
    # GetWindowText can read a foreign caption, but not foreign control text.
    # Native TaskDialog bodies also need not expose text through child HWNDs.
    # The caller verifies the full warning reason independently in the log.
    return next((h for h in handles if is_visible(h) and get_class(h) == "#32770"
                 and get_title(h) == "Aegisub Warning"), None)


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
        ("GetParent", [W.HWND], W.HWND),
        ("GetWindowRect", [W.HWND, C.POINTER(W.RECT)], W.BOOL),
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

    def rect(hwnd):
        value = W.RECT()
        assert user.GetWindowRect(hwnd, C.byref(value))
        return value.left, value.top, value.right, value.bottom

    def progress_control(frame):
        return next((h for h in windows(process.pid, frame)
                     if text(h, True).lower() == "msctls_progress32" and user.IsWindowVisible(h)), None)

    measurements = []
    for scenario in ("empty", "success", "scripts", "conflicts", "cancel", "Mangetsu", "libassmod", "missing"):
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
        if scenario in ("success", "scripts"):
            (autoload / "000-fast.lua").write_text("aegisub.register_macro('Smoke First', '', function() end)", encoding="utf-8")
        if scenario == "success":
            (autoload / "slow.lua").write_text("local deadline=os.clock()+2; while os.clock()<deadline do end; aegisub.register_macro('Smoke Slow', '', function() end)", encoding="utf-8")
        elif scenario == "scripts":
            (autoload / "a.lua").write_text("""
local deadline = os.clock() + 2
while os.clock() < deadline do end
require('aegisub.clipboard').get()
aegisub.register_macro('Smoke Lua', '', function()
    local f = assert(io.open(os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/lua-ran', 'w'))
    f:write('ok'); f:close()
end)
aegisub.register_macro('Smoke Held', '', function()
    local function mark(name)
        local f = assert(io.open(os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/' .. name, 'w'))
        f:write('ok'); f:close()
    end
    mark('held-started')
    local deadline = os.clock() + 6
    while os.clock() < deadline do end
    mark('held-finished')
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
            for index in range(4):
                (autoload / f"infinite-{index}.lua").write_text("while true do end", encoding="utf-8")
        elif scenario == "conflicts":
            second = profile / "second-autoload"
            second.mkdir(exist_ok=True)
            config["Path"]["Automation"]["Autoload"] = str(autoload) + "|" + str(second) + "|" + str(autoload)
            (profile / "config.json").write_text(json.dumps(config), encoding="utf-8")
            def conflicting_script(marker, slow=False):
                return ("local deadline=os.clock()+2; while os.clock()<deadline do end; " if slow else "") + f"""
aegisub.register_macro('Duplicate', '', function()
    local f = assert(io.open(os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/winner', 'w'))
    f:write('{marker}'); f:close()
end)
aegisub.register_filter('Duplicate filter', 'staged export filter', 100, function() end)
"""
            (autoload / "shared.lua").write_text(conflicting_script("first", slow=True), encoding="utf-8")
            (second / "shared.lua").write_text(conflicting_script("second"), encoding="utf-8")
        env = dict(os.environ, CI="true", AEGISUB_STARTUP_SMOKE_DIR=str(profile), AEGISUB_AUTOMATION_WORKERS="4")
        started = time.monotonic()
        process = subprocess.Popen([str(profile / "aegisub.exe")], cwd=profile, env=env)
        try:
            frame = wait(lambda: visible_main_frame(windows(process.pid), user.GetMenu, user.IsWindowVisible), "visible main frame")
            assert user.IsWindowVisible(frame), "Main frame must be visible"
            send(frame, 0)
            visible_ms = (time.monotonic() - started) * 1000

            def logs():
                return "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in (profile / "log").glob("*.json"))

            if scenario in ("success", "scripts"):
                gauge = wait(lambda: progress_control(frame), "progress before completion")
                status = user.GetParent(user.GetParent(gauge))
                assert "statusbar" in text(status, True).lower(), "Progress must belong to the status bar"
                assert rect(gauge)[1] >= rect(status)[1] and rect(gauge)[3] <= rect(status)[3]
                assert rect(gauge)[3] - rect(gauge)[1] < rect(status)[3] - rect(status)[1], "Thin single-row gauge"
                assert send(gauge, 0x407) == (2 if scenario == "success" else 4)  # PBM_GETRANGE, upper bound
                wait(lambda: any(name == "Smoke First" for name, _ in entries(user.GetMenu(frame))), "first macro before slow initialization completes")
                assert not re.search(r'"section"\s*:\s*"startup/automation"', logs()), "First macro must be usable before the batch completes"
            if scenario == "success":
                excluded = set(windows(process.pid, status)) | {status}
                geometry = {h: rect(h) for h in windows(process.pid, frame) if h not in excluded and user.IsWindowVisible(h)}
                wait(lambda: "2 / 2" in logs(), "successful completion")
                wait(lambda: send(gauge, 0x408) == 2, "native gauge reaches 2 / 2, including all completions")
                wait(lambda: not progress_control(frame), "successful progress automatically collapses", seconds=8)
                assert geometry == {h: rect(h) for h in geometry}, "Progress must not resize the grid or editor"
            elif scenario == "scripts":
                # Editing works during initialization, without moving keyboard focus.
                edit = wait(lambda: next((h for h in windows(process.pid, frame) if "scintilla" in text(h, True).lower() and user.IsWindowVisible(h)), None), "subtitle editor")
                length = send(edit, 2183)  # SCI_GETTEXTLENGTH
                send(edit, 0x102, ord("x"))  # WM_CHAR; no cross-process pointer.
                assert send(edit, 2183) == length + 1
                send(frame, 0)
                wait(lambda: "4 / 4" in logs(), "four scripts completed, including the invalid script")
                wait(lambda: send(gauge, 0x408) == 4, "native gauge includes failed initialization")
                assert any(text(h) == "Automation (!)" and user.IsWindowVisible(h) for h in windows(process.pid, frame)), "Subtle failure indicator stays visible"
                assert "failures: 1" in logs()
                assert "unsupported extension" not in logs()
                assert not any(name == "Must not publish" for name, _ in entries(user.GetMenu(frame)))
                for label, marker in (("Smoke Lua", "lua-ran"), ("Smoke Moon", "moon-ran")):
                    invoke(frame, label)
                    wait(lambda: (profile / marker).exists(), label + " execution")
                # Clicking the status strip opens the existing error/details UI.
                user.PostMessageW(gauge, 0x202, 0, 0)  # WM_LBUTTONUP
                manager = wait(lambda: next((h for h in windows(process.pid) if text(h) == "Automation Manager"), None), "Automation Manager")
                rescan = wait(lambda: next((h for h in windows(process.pid, manager) if "scan Autoload Dir" in text(h)), None), "Rescan button")
                for _ in range(3):
                    user.PostMessageW(rescan, 0xF5, 0, 0)  # BM_CLICK, repeated rescans coalesce.
                    time.sleep(0.05)
                wait(lambda: progress_control(frame), "manual progress")
                wait(lambda: logs().count('"section" : "startup/automation"') >= 2 or len(re.findall(r'"section"\s*:\s*"startup/automation"', logs())) >= 2, "rescan completion")
                # A completed replacement must not destroy a Lua state still running
                # a macro in DialogProgress's nested event loop.
                user.PostMessageW(rescan, 0xF5, 0, 0)
                invoke(frame, "Smoke Held")
                wait(lambda: (profile / "held-started").exists(), "held macro started")
                wait(lambda: 'publication deferred until Automation/export completes' in logs(), "publication waits for the running macro")
                assert not (profile / "held-finished").exists()
                send(frame, 0)
                wait(lambda: (profile / "held-finished").exists(), "held macro finished")
                wait(lambda: len(re.findall(r'"section"\s*:\s*"startup/automation"', logs())) >= 3, "publication after macro completion")
                user.PostMessageW(manager, 0x10, 0, 0)
                for label in ("Smoke Lua", "Smoke Moon"):
                    assert sum(name == label for name, _ in entries(user.GetMenu(frame))) == 1
                # Avoid a save prompt after the intentional edit; undo via SCI_UNDO.
                send(edit, 2176)
            elif scenario == "conflicts":
                wait(lambda: "2 / 2" in logs(), "duplicate-name scripts completed")
                assert sum(name == "Duplicate" for name, _ in entries(user.GetMenu(frame))) == 1
                invoke(frame, "Duplicate")
                wait(lambda: (profile / "winner").exists(), "duplicate macro execution")
                assert (profile / "winner").read_text() == "second", "Configured directory order wins, not worker finish order"
                (second / "shared.lua").write_text(conflicting_script("newest"), encoding="utf-8")
                invoke(frame, "Automation...")
                manager = wait(lambda: next((h for h in windows(process.pid) if text(h) == "Automation Manager"), None), "Automation Manager")
                rescan = wait(lambda: next((h for h in windows(process.pid, manager) if "scan Autoload Dir" in text(h)), None), "Rescan button")
                for _ in range(3):
                    user.PostMessageW(rescan, 0xF5, 0, 0)
                    time.sleep(0.05)
                wait(lambda: len(re.findall(r'"section"\s*:\s*"startup/automation"', logs())) >= 2, "newest conflict rescan completed")
                user.PostMessageW(manager, 0x10, 0, 0)
                assert sum(name == "Duplicate" for name, _ in entries(user.GetMenu(frame))) == 1
                invoke(frame, "Duplicate")
                wait(lambda: (profile / "winner").read_text() == "newest", "newest generation replaces old macros")
            elif scenario == "cancel":
                wait(lambda: progress_control(frame), "four infinite initializers started")
                send(frame, 0)
            else:
                wait(lambda: "startup/automation" in logs(), "empty autoload completion")
                assert not any(text(h, True).lower() == "msctls_progress32" and user.IsWindowVisible(h) for h in windows(process.pid, frame)), "No progress bar for empty directories"
                invoke(frame, "Use Dummy Video...")
                dialog = wait(lambda: next((h for h in windows(process.pid) if "dummy video options" in text(h).lower()), None), "dummy video dialog")
                ok = wait(lambda: next((h for h in windows(process.pid, dialog) if text(h).replace("&", "") == "OK"), None), "dummy video OK")
                user.PostMessageW(ok, 0xF5, 0, 0)
                if scenario == "missing":
                    wait(lambda: "Mangetsu unavailable. Trying fallback subtitle renderers." in logs(), "missing renderer warning reason")
                    warning = wait(lambda: visible_warning_dialog(windows(process.pid), text,
                                                                  lambda h: text(h, True), user.IsWindowVisible),
                                   "visible missing renderer warning")
                    user.PostMessageW(warning, 0x10, 0, 0)
                    wait(lambda: not user.IsWindowVisible(warning), "missing renderer warning dismissed")
                    wait(lambda: re.search(r"libass: [0-9.eE+-]+ ms \(provider initialization\)", logs()), "libass fallback initialized")
                wait(lambda: "startup/renderer" in logs(), "selected renderer creation")
                if scenario in ("empty", "missing"):
                    assert "libassmod loaded from" not in logs()
                    assert "Mangetsu loaded from" not in logs()
                elif scenario == "Mangetsu":
                    assert "Mangetsu loaded from" in logs() and "libassmod loaded from" not in logs()
                else:
                    assert "libassmod loaded from" in logs() and "Mangetsu loaded from" not in logs()
                if scenario in ("empty", "missing"):
                    if scenario == "missing":
                        shutil.copy2(portable / "mangetsu.dll", profile / "mangetsu.dll")
                    invoke(frame, "Options...")
                    preferences = wait(lambda: next((h for h in windows(process.pid) if text(h) == "Preferences"), None), "renderer selection UI")
                    wait(lambda: "Mangetsu loaded from" in logs() and "libassmod loaded from" in logs(), "UI discovery retries failed availability")
                    user.PostMessageW(preferences, 0x10, 0, 0)

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
            (output / "measurements.json").write_text(json.dumps(measurements, indent=2), encoding="utf-8")
            assert "First responsive idle event" in logs()
            if scenario == "missing":
                assert json.loads((profile / "config.json").read_text(encoding="utf-8-sig"))["Subtitle"]["Provider"] == "Mangetsu"
        except BaseException:
            print(json.dumps({"scenario": scenario, "exit_code": process.poll(),
                              "windows": [{"title": text(h), "class": text(h, True),
                                           "visible": bool(user.IsWindowVisible(h)),
                                           "menu": bool(user.GetMenu(h))}
                                          for h in windows(process.pid)]}, indent=2), flush=True)
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--portable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.portable.resolve(), args.output.resolve())
