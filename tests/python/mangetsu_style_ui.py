"""Real wxWidgets style/metadata transactions, run by packaged Windows smoke CI.

Only public UI and an ordinary Automation macro are used; no product test hooks.
"""
import ctypes as C
from ctypes import wintypes as W
import os
import subprocess
import unicodedata
from native_gui_snapshot import Control, StaleWindow, control_snapshot, editor_row, enable_editor, notification_wparam


def prepare(profile, autoload):
    fixture = profile / "style-ui.ass"
    fixture.write_text(r"""[Script Info]
ScriptType: v4.00+
PlayResX: 320
PlayResY: 180
WrapStyle: 0

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Comment: 0,0:00:00.00,0:00:00.00,Default,Nene,0,0,0,mangetsu-colorcoding,{\1c&HFFFFFF&}
Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,mangetsu-style,{\furis70\q2\grd(45,&HFF0000&,&H0000FF&)}
Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,mangetsu-style,{\vc(&HFFFFFF&,&H000000&,&HFFFFFF&,&H000000&)}
Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,note,Ordinary comment 70
Dialogue: 0,0:00:00.00,0:00:04.00,Default,Nene,0,0,0,,မြန်မာ <漢字|かんじ> 日本語
""", encoding="utf-8")
    (autoload / "style-ui.lua").write_text(r"""
aegisub.register_macro('Style UI Snapshot', '', function(subs, sel, active)
    local path = os.getenv('AEGISUB_STARTUP_SMOKE_DIR') .. '/style-ui-snapshot'
    local f = assert(io.open(path .. '.tmp', 'w'))
    for i = 1, #subs do
        local line = subs[i]
        if line.class == 'style' then f:write('STYLE|' .. line.name .. '\n') end
        if line.class == 'dialogue' then
            f:write('LINE|' .. line.effect .. '|' .. line.style .. '|' .. line.text .. '\n')
        end
    end
    for _, i in ipairs(sel) do f:write('SELECTED|' .. subs[i].effect .. '\n') end
    if active then f:write('ACTIVE|' .. subs[active].effect .. '\n') end
    f:close()
    assert(os.rename(path .. '.tmp', path))
    return sel, active
end)
aegisub.register_macro('Select style metadata', '', function(subs)
    for i = 1, #subs do
        if subs[i].class == 'dialogue' and subs[i].effect == 'mangetsu-style' then return {i}, i end
    end
end)
aegisub.register_macro('Select actor metadata', '', function(subs)
    for i = 1, #subs do
        if subs[i].class == 'dialogue' and subs[i].effect == 'mangetsu-colorcoding' then return {i}, i end
    end
end)
""", encoding="utf-8")
    return fixture


def exercise(profile, pid, frame, user, windows, text, wait, send, entries, invoke, rect):
    def label(h):
        return text(h).replace("&", "")

    def normalized_label(value):
        # Win32/wx can display native mnemonic markers and different
        # ellipsis glyphs while keeping the same user-visible command.
        return unicodedata.normalize("NFKC", value.replace("&", "")).replace("…", "...").strip().casefold()

    def identity(hwnd):
        owner = W.DWORD()
        thread = user.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if not thread or owner.value != pid or not user.IsWindow(hwnd):
            raise StaleWindow(f"Window {hwnd} no longer belongs to the application")
        return owner.value, thread, user.GetDlgCtrlID(hwnd), user.GetParent(hwnd)

    def rect(hwnd):
        identity(hwnd)
        value = W.RECT()
        if not user.GetWindowRect(hwnd, C.byref(value)):
            if C.get_last_error() == 1400 or not user.IsWindow(hwnd):
                raise StaleWindow(f"Window {hwnd} disappeared during GetWindowRect")
            raise C.WinError(C.get_last_error())
        return value.left, value.top, value.right, value.bottom

    def send(hwnd, message, wparam=0, lparam=0, timeout=1000):
        identity(hwnd)
        result = C.c_size_t()
        if not user.SendMessageTimeoutW(hwnd, message, wparam, lparam, 2, timeout, C.byref(result)):
            if C.get_last_error() == 1400 or not user.IsWindow(hwnd):
                raise StaleWindow(f"Window {hwnd} disappeared during message {message:#x}")
            raise AssertionError(f"Native message {message:#x} failed or timed out for {hwnd}")
        return result.value

    def scan(parent):
        def describe(hwnd):
            stamp = identity(hwnd)
            if not user.IsChild(parent, hwnd):
                raise StaleWindow("Control moved to another parent")
            if not user.IsWindowVisible(hwnd):
                return None
            bounds = rect(hwnd)
            if bounds[2] <= bounds[0] or bounds[3] <= bounds[1]:
                return None  # Newly constructed, not laid out yet.
            return Control(hwnd, label(hwnd), text(hwnd, True), bounds,
                           bool(user.IsWindowEnabled(hwnd)), stamp)

        def current(control):
            return (identity(control.hwnd) == control.identity and user.IsChild(parent, control.hwnd)
                    and user.IsWindowVisible(control.hwnd) and label(control.hwnd) == control.label
                    and text(control.hwnd, True) == control.kind and rect(control.hwnd) == control.bounds)

        return control_snapshot(windows(pid, parent), describe, current)

    def inventory(parent):
        # The wrapper also represents a valid empty snapshot without extending
        # the caller's timeout or nesting a second polling loop inside a read.
        return wait(lambda: (s,) if (s := scan(parent)) is not None else None,
                    "stable native control inventory")[0]

    def required(parent, captions, seconds=30):
        def ready():
            snapshot = scan(parent)
            if snapshot is None:
                return None
            result = []
            for caption in captions:
                matches = [c for c in snapshot if normalized_label(c.label) == normalized_label(caption)]
                if len(matches) != 1:
                    return None
                result.append(matches[0])
            return result
        return wait(ready, "complete native controls: " + ", ".join(captions), seconds=seconds)

    def controls_of_kind(parent, kind, count=None):
        def ready():
            snapshot = scan(parent)
            if snapshot is None:
                return None
            matches = [c for c in snapshot if c.kind.lower() == kind]
            return matches if matches and (count is None or len(matches) == count) else None
        return wait(ready, "complete native " + kind + " controls")

    def button(parent, caption, rightmost=False):
        def ready():
            if not user.IsWindowEnabled(parent):
                return None
            snapshot = scan(parent)
            if snapshot is None:
                return None
            matches = [c for c in snapshot if c.kind.lower() == "button" and c.enabled
                       and normalized_label(c.label) == normalized_label(caption)]
            if matches:
                return max(matches, key=lambda c: c.bounds[0]).hwnd if rightmost else matches[0].hwnd
            return None
        return wait(ready, caption + " enabled native button")

    def click(parent, caption, rightmost=False):
        h = button(parent, caption, rightmost)
        assert user.PostMessageW(h, 0xF5, 0, 0), "Native button disappeared before click"

    def click_sync(parent, caption):
        # Only for non-modal handlers such as Apply. Unlike BM_CLICK posted
        # asynchronously, SendMessageTimeout waits for the native handler.
        send(button(parent, caption), 0xF5, timeout=30000)

    def dialog(caption):
        return wait(lambda: next((h for h in windows(pid) if normalized_label(text(h)) == normalized_label(caption)
                                  and user.IsWindowVisible(h) and user.IsWindowEnabled(h)), None), caption)

    def close(parent, caption="Cancel"):
        click(parent, caption)
        wait(lambda: not user.IsWindowVisible(parent), caption + " closes dialog")

    def get_text(h):
        buf = C.create_unicode_buffer(2048)
        send(h, 0xD, len(buf), C.addressof(buf))  # WM_GETTEXT marshalled by Windows
        return buf.value

    def set_text(h, value):
        buf = C.create_unicode_buffer(value)
        send(h, 0xC, 0, C.addressof(buf))  # WM_SETTEXT, not a WM_USER pointer

    def choices(h):
        if "combobox" not in text(h, True).lower():
            return []
        count = send(h, 0x146)  # CB_GETCOUNT
        if count > 100:
            return []
        values = []
        for i in range(count):
            buf = C.create_unicode_buffer(2048)
            send(h, 0x148, i, C.addressof(buf))  # CB_GETLBTEXT
            values.append(buf.value)
        return values

    def choice_control(parent, predicate):
        def ready():
            snapshot = scan(parent)
            if snapshot is None:
                return None
            try:
                for control in snapshot:
                    if (control.kind.lower() == "combobox" and predicate(choices(control.hwnd))
                            and identity(control.hwnd) == control.identity):
                        return control.hwnd
            except StaleWindow:
                return None
            return None
        return wait(ready, "complete native choice control")

    def choose(h, index):
        send(h, 0x14E, index)  # CB_SETCURSEL
        # Notify the owning dialog synchronously. Asynchronous selection
        # events otherwise race the immediately following Apply/Cancel.
        send(user.GetParent(h), 0x111, notification_wparam(user.GetDlgCtrlID(h), 1), h, timeout=30000)

    def enabled_in_dialog(hwnd, parent):
        # IsWindowEnabled alone ignores a disabled parent panel (offset pairs).
        while hwnd:
            if not user.IsWindowEnabled(hwnd):
                return False
            if hwnd == parent:
                return True
            hwnd = user.GetParent(hwnd)
        return False

    def row(parent, caption):
        # Switching the border layer destroys and recreates the whole native
        # form. A label may appear before its checkbox/input: treat the trio
        # as one atomic readiness condition, not three separate lookups.
        return wait(lambda: editor_row(scan(parent), caption, normalized_label),
                    caption + " complete native editor row")

    def wait_row_value(parent, caption, value, assigned):
        def ready():
            handles = editor_row(scan(parent), caption, normalized_label)
            if handles is None:
                return None
            try:
                return (get_text(handles[1]) == value and user.IsWindowEnabled(handles[0])
                        and enabled_in_dialog(handles[1], parent) == assigned)
            except StaleWindow:
                return None
        return wait(ready, caption + " value after completed form switch")

    def assign(parent, caption, value, choice=False):
        field = enable_editor(lambda: row(parent, caption), lambda h: enabled_in_dialog(h, parent),
                              lambda h: send(h, 0xF5, timeout=30000))
        if choice:
            choose(field, value)
        else:
            set_text(field, value)

    def snapshot():
        target = profile / "style-ui-snapshot"
        target.unlink(missing_ok=True)
        command("Style UI Snapshot")
        return wait(lambda: target.read_text(encoding="utf-8") if target.exists() and target.stat().st_size else None, "metadata snapshot")

    def capture(hwnd, name):
        # Print the actual native dialog (including children), using only .NET
        # already on hosted Windows. No Pillow or other runtime dependency.
        bounds = rect(hwnd)
        env = dict(os.environ, AEGISUB_UI_HWND=str(hwnd), AEGISUB_UI_WIDTH=str(bounds[2] - bounds[0]),
                   AEGISUB_UI_HEIGHT=str(bounds[3] - bounds[1]), AEGISUB_UI_PATH=str(profile / (name + ".png")))
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", r"""
Add-Type -AssemblyName System.Drawing
Add-Type 'using System; using System.Runtime.InteropServices; public class DialogCapture { [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags); }'
$bitmap = New-Object System.Drawing.Bitmap ([int]$env:AEGISUB_UI_WIDTH), ([int]$env:AEGISUB_UI_HEIGHT)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try { if (-not [DialogCapture]::PrintWindow([IntPtr][long]$env:AEGISUB_UI_HWND, $dc, 2)) { throw 'PrintWindow failed' } }
finally { $graphics.ReleaseHdc($dc) }
$bitmap.Save($env:AEGISUB_UI_PATH, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose(); $bitmap.Dispose()
"""], env=env, check=True, timeout=30, stdout=subprocess.DEVNULL)

    def editor(copy=False):
        invoke(frame, "Styles Manager...")
        manager = dialog("Styles Manager")
        click(manager, "Copy" if copy else "Edit", rightmost=True)
        return manager, dialog("Style Editor")

    def close_manager(manager):
        wait(lambda: user.IsWindowEnabled(manager), "Styles Manager ready to close")
        assert user.PostMessageW(manager, 0x10, 0, 0)
        wait(lambda: not user.IsWindowVisible(manager), "Styles Manager closes")

    def select_style(manager, name):
        lists = controls_of_kind(manager, "listbox")
        current = max(lists, key=lambda c: c.bounds[0]).hwnd
        for index in range(send(current, 0x18B)):  # LB_GETCOUNT
            buf = C.create_unicode_buffer(2048)
            send(current, 0x189, index, C.addressof(buf))  # LB_GETTEXT
            if buf.value == name:
                send(current, 0x185, 0, -1)  # LB_SETSEL: clear, then select
                send(current, 0x185, 1, index)
                send(user.GetParent(current), 0x111, notification_wparam(user.GetDlgCtrlID(current), 1), current, timeout=30000)
                return
        raise AssertionError("Style missing: " + name)

    def command_id(identifier):
        # These commands require no user input. Wait for the complete native
        # handler, including LuaThreadedCall's modal loop and selection update.
        # A file written inside a macro is not a completion notification: posting
        # another WM_COMMAND then can bypass the disabled frame and reenter Lua.
        wait(lambda: user.IsWindowEnabled(frame), "main frame ready for command")
        send(frame, 0x111, identifier, timeout=30000)

    def command(caption):
        matches = [identifier for name, identifier in entries(user.GetMenu(frame)) if name == caption]
        assert len(matches) == 1, (caption, matches)
        command_id(matches[0])

    initial = snapshot()
    assert "SELECTED|mangetsu-colorcoding" in initial and "ACTIVE|mangetsu-colorcoding" in initial
    assert sum(line.startswith("LINE|mangetsu-") for line in initial.splitlines()) == 3
    command("Select actor metadata")
    assert "ACTIVE|mangetsu-colorcoding" in snapshot(), "Actor Colorcoding rows stay visible and selectable"
    command("Show Mangetsu Metadata")
    command("Select style metadata")
    assert "ACTIVE|mangetsu-style" in snapshot()
    command("Show Mangetsu Metadata")
    hidden = snapshot()
    assert "ACTIVE|mangetsu-style" not in hidden and "SELECTED|mangetsu-style" not in hidden
    assert "ACTIVE|mangetsu-colorcoding" in hidden, "Hiding style metadata leaves actor declarations visible"
    assert [s for s in initial.splitlines() if s.startswith("LINE|")] == [s for s in hidden.splitlines() if s.startswith("LINE|")]
    sorts = [command for name, command in entries(user.GetMenu(frame)) if name == "Actor Name"]
    assert len(sorts) == 2
    command_id(sorts[0])  # Sort all; preserve the ordered metadata header.
    ordered = snapshot()
    assert [s for s in ordered.splitlines() if s.startswith("LINE|")][:3] == [s for s in initial.splitlines() if s.startswith("LINE|")][:3]
    invoke(frame, "Find and Replace...")
    replace = dialog("Replace")
    fields = sorted(controls_of_kind(replace, "combobox", 2), key=lambda c: c.bounds[1])
    set_text(fields[0].hwnd, "70")
    set_text(fields[1].hwnd, "71")
    click(replace, "Replace all")
    def replacement_result():
        for hwnd in windows(pid):
            if hwnd not in (frame, replace) and user.IsWindowVisible(hwnd) and user.IsWindowEnabled(hwnd):
                snapshot = scan(hwnd)
                if snapshot and any(normalized_label(c.label) == "ok" for c in snapshot):
                    return hwnd
        return None
    message = wait(replacement_result, "replacement result")
    close(message, "OK")
    close(replace)
    replaced = snapshot()
    assert "Ordinary comment 71" in replaced, "Ordinary comments remain editable"
    assert "\\furis70" in replaced and "\\furis71" not in replaced, "Hidden style metadata is excluded from Replace All"

    manager, style = editor()
    # wxWidgets may publish a native dialog window before all children have
    # finished showing. Wait for its actual controls, but never ignore an
    # absent button: report which one is missing and retain a UI screenshot.
    expected = ("Furigana…", "Vertical…", "Multi-border…", "OK", "Cancel", "Apply", "Help")
    try:
        controls = required(style, expected, seconds=10)
    except AssertionError as exc:
        # A diagnostic must not raise the same stale-HWND error it reports.
        native_dump = scan(style)
        missing = [n for n in expected if native_dump is None or not any(
            normalized_label(c.label) == normalized_label(n) for c in native_dump)]
        diagnostic = profile / "style-ui-result.txt"
        diagnostic.write_text(
            "Missing Style Editor controls: " + repr(missing) + "\n"
            + "Native control snapshot:\n" + repr(native_dump) + "\n",
            encoding="utf-8",
        )
        try:
            capture(style, "style-editor-missing-controls")
        except Exception as capture_error:
            diagnostic.write_text(diagnostic.read_text(encoding="utf-8")
                                  + "Screenshot failed: " + repr(capture_error) + "\n", encoding="utf-8")
        raise AssertionError(f"Style Editor is missing visible controls {missing}; "
                             f"native child inventory: {native_dump}") from exc
    wrap = choice_control(style, lambda values: values == ["Inherit Script", "Smart", "End-of-line", "No automatic wrap", "Alternative smart"])
    layout = {c.hwnd: c.bounds for c in inventory(style)}
    centers = [sum(layout[h][1::2]) / 2 for h in [c.hwnd for c in controls] + [wrap]]
    assert max(centers) - min(centers) < 5, "All eight controls must share one bottom row"
    assert layout[controls[2].hwnd][2] <= layout[wrap][0] < min(layout[c.hwnd][0] for c in controls[3:]), "Wrap belongs to left group"
    assert send(wrap, 0x147) == 3, "Existing q2 must be selected"  # CB_GETCURSEL
    capture(style, "style-editor")
    click(style, "Furigana…")
    furi = dialog("Furigana")
    assert get_text(row(furi, "Reading scale X (%)")[1]) == "70"
    assign(furi, "Reading scale X (%)", "80")
    close(furi)
    click(style, "Furigana…")
    furi = dialog("Furigana")
    assert get_text(row(furi, "Reading scale X (%)")[1]) == "70", "Child Cancel discards edits"
    assign(furi, "Reading scale X (%)", "80")
    capture(furi, "furigana")
    close(furi, "OK")
    close(style)
    close_manager(manager)
    assert "\\furis70" in snapshot(), "Parent Cancel discards accepted child edits"

    manager, style = editor()
    click(style, "Furigana…")
    furi = dialog("Furigana")
    assign(furi, "Reading scale X (%)", "80")
    close(furi, "OK")
    click(style, "Vertical…")
    vertical = dialog("Vertical layout")
    assign(vertical, "Native vertical layout", 1, choice=True)
    assign(vertical, "Vertical unit policy", 3, choice=True)
    capture(vertical, "vertical")
    close(vertical, "OK")
    click(style, "Multi-border…")
    border = dialog("Multi-border")
    required(border, ("Border alpha (0–255)",))
    # A radio handler saves the old layer, destroys its controls and builds
    # the new form. Fence that entire operation before any new HWND lookup.
    click_sync(border, "10")
    wait_row_value(border, "Border size X", "0", False)
    assign(border, "Border size X", "-1")
    click(border, "OK")
    close(dialog("Invalid style value"), "OK")
    assert user.IsWindowVisible(border), "Invalid numbers must not close or commit the editor"
    assign(border, "Border size X", "3")
    capture(border, "multi-border")
    close(border, "OK")
    wrap = choice_control(style, lambda values: "Inherit Script" in values)
    choose(wrap, 1)  # q0; metadata only, not Script Info WrapStyle
    click_sync(style, "Apply")
    assert user.IsWindowVisible(style), "Apply leaves Style Editor open"
    choose(wrap, 0)  # Pending reset must be discarded by Cancel after Apply.
    close(style)
    close_manager(manager)
    committed = snapshot()
    for tag in ("\\furis80", "\\vert1", "\\vtype3", "\\10bs3", "\\q0", "\\grd(45,", "\\vc("):
        assert tag in committed, (tag, committed)
    assert "LINE|mangetsu-colorcoding|Default|{\\1c&HFFFFFF&}" in committed

    def history(prefix):
        matches = [(label, command) for label, command in entries(user.GetMenu(frame)) if label.startswith(prefix)]
        assert len(matches) == 1, matches
        command_id(matches[0][1])

    history("Undo")
    assert "\\furis70" in snapshot(), "One undo restores ordinary style and metadata"
    history("Redo")
    assert "\\furis80" in snapshot()
    manager, style = editor(copy=True)
    edits = controls_of_kind(style, "edit")
    set_text(min(edits, key=lambda c: (c.bounds[1], c.bounds[0])).hwnd, "UI Copy")
    close(style, "OK")
    close_manager(manager)
    assert "LINE|mangetsu-style|UI Copy|" in snapshot(), "Copy includes inherited metadata"
    invoke(frame, "Styles Manager...")
    manager = dialog("Styles Manager")
    select_style(manager, "UI Copy")
    click(manager, "Delete", rightmost=True)
    confirm = dialog("Confirm delete from current")
    close(confirm, "Yes")
    close_manager(manager)
    assert "LINE|mangetsu-style|UI Copy|" not in snapshot(), "Delete removes only copied declarations"

    manager, style = editor()
    edits = controls_of_kind(style, "edit")
    set_text(min(edits, key=lambda c: (c.bounds[1], c.bounds[0])).hwnd, "UI Sign")
    click(style, "OK")
    close(dialog("Update script?"), "Yes")
    wait(lambda: not user.IsWindowVisible(style), "renamed Style Editor closes")
    close_manager(manager)
    assert "LINE|mangetsu-style|UI Sign|" in snapshot()
    command("Save Subtitles")
    wait(lambda: "\\furis80" in (profile / "style-ui.ass").read_text(encoding="utf-8-sig"), "committed metadata saved")
    saved = (profile / "style-ui.ass").read_text(encoding="utf-8-sig")
    assert "WrapStyle: 0" in saved and "mangetsu-colorcoding" in saved

    invoke(frame, "Options...")
    preferences = dialog("Preferences")
    renderer = choice_control(preferences, lambda values: {"Mangetsu", "libass"}.issubset(values))
    choose(renderer, choices(renderer).index("libass"))
    close(preferences, "OK")
    wait(lambda: not any(name == "Show Mangetsu Metadata" for name, _ in entries(user.GetMenu(frame))), "renderer switch hides metadata menu")
    manager, style = editor()
    hidden_names = {normalized_label(n) for n in ("Furigana…", "Vertical…", "Multi-border…", "Wrap Mode")}
    assert not any(normalized_label(c.label) in hidden_names for c in inventory(style))
    capture(style, "style-editor-libass")
    close(style)
    close_manager(manager)
    assert "\\furis80" in snapshot(), "Renderer switching preserves saved metadata"
    (profile / "style-ui-result.txt").write_text("metadata visibility, sort, replace, transactions, layout, copy/delete/rename, undo/redo, save, renderer switch: passed\n", encoding="utf-8")
