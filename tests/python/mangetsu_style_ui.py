"""Real wxWidgets style/metadata transactions, run by packaged Windows smoke CI.

Only public UI and an ordinary Automation macro are used; no product test hooks.
"""
import ctypes as C
from ctypes import wintypes as W
import os
import subprocess
import unicodedata
from native_gui_snapshot import Control, StaleWindow, control_snapshot, editor_row, notification_wparam


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
aegisub.register_macro('Select first dialogue', '', function(subs)
    for i = 1, #subs do
        if subs[i].class == 'dialogue' and not subs[i].comment then return {i}, i end
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
        assert send(h, 0x14E, index) == index  # CB_SETCURSEL
        # wxChoice and wxComboBox dispatch their wx events on CBN_SELENDOK.
        send(user.GetParent(h), 0x111, notification_wparam(user.GetDlgCtrlID(h), 9), h, timeout=30000)
        assert send(h, 0x147) == index  # CB_GETCURSEL

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
        # form. A label may appear before its input or reset: treat the trio
        # as one atomic readiness condition, not three separate lookups.
        return wait(lambda: editor_row(scan(parent), caption, normalized_label),
                    caption + " complete native editor row")

    def wait_row_value(parent, caption, value, enabled=True):
        def ready():
            handles = editor_row(scan(parent), caption, normalized_label)
            if handles is None:
                return None
            try:
                return get_text(handles[0]) == value and enabled_in_dialog(handles[0], parent) == enabled
            except StaleWindow:
                return None
        return wait(ready, f"{caption} shows {value!r} (editable={enabled})")

    def assign(parent, caption, value, choice=False):
        field, _ = row(parent, caption)
        assert enabled_in_dialog(field, parent), caption + " is directly editable"
        if choice:
            choose(field, value)
        else:
            set_text(field, value)
            send(user.GetParent(field), 0x111, notification_wparam(user.GetDlgCtrlID(field), 0x300), field)

    def reset(parent, caption):
        _, control = row(parent, caption)
        assert enabled_in_dialog(control, parent), caption + " can be reset"
        send(control, 0xF5, timeout=30000)

    def assert_direct_form(parent, fields):
        controls = inventory(parent)
        assert sum(normalized_label(c.label) == "reset" for c in controls) == fields
        assert not any(normalized_label(c.label) == "override" for c in controls)

    def snapshot():
        target = profile / "style-ui-snapshot"
        target.unlink(missing_ok=True)
        command("Style UI Snapshot")
        return wait(lambda: target.read_text(encoding="utf-8") if target.exists() and target.stat().st_size else None, "metadata snapshot")

    def capture(hwnd, name, samples=()):
        # Print the actual native dialog (including children), using only .NET
        # already on hosted Windows. No Pillow or other runtime dependency.
        bounds = rect(hwnd)
        env = dict(os.environ, AEGISUB_UI_HWND=str(hwnd), AEGISUB_UI_WIDTH=str(bounds[2] - bounds[0]),
                   AEGISUB_UI_HEIGHT=str(bounds[3] - bounds[1]), AEGISUB_UI_PATH=str(profile / (name + ".png")),
                   AEGISUB_UI_SAMPLES=";".join(f"{x - bounds[0]},{y - bounds[1]}" for x, y in samples))
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type 'using System; using System.Runtime.InteropServices; public class DialogCapture { [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags); }'
$bitmap = New-Object System.Drawing.Bitmap ([int]$env:AEGISUB_UI_WIDTH), ([int]$env:AEGISUB_UI_HEIGHT)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try { if (-not [DialogCapture]::PrintWindow([IntPtr][long]$env:AEGISUB_UI_HWND, $dc, 2)) { throw 'PrintWindow failed' } }
finally { $graphics.ReleaseHdc($dc) }
$bitmap.Save($env:AEGISUB_UI_PATH, [System.Drawing.Imaging.ImageFormat]::Png)
if ($env:AEGISUB_UI_SAMPLES) {
    foreach ($point in $env:AEGISUB_UI_SAMPLES.Split(';')) {
        $x, $y = $point.Split(',')
        $pixel = $bitmap.GetPixel([int]$x, [int]$y)
        '{0:X2}{1:X2}{2:X2}' -f $pixel.R, $pixel.G, $pixel.B
    }
}
$graphics.Dispose(); $bitmap.Dispose()
"""], env=env, check=True, timeout=30, capture_output=True, text=True)
        return result.stdout.split()

    def assert_native_palette(parent, branded, normal):
        def samples(bounds):
            left, top, right, bottom = bounds
            return [(left + (right - left) * x // 8, top + (bottom - top) * y // 8)
                    for x in (2, 4, 6) for y in (2, 4, 6)]
        # PrintWindow includes the rendered child controls. In wx master the
        # system dark-mode button may itself use wx's own drawing path.
        controls = branded + normal
        pixels = capture(parent, "style-editor", [point for c in controls for point in samples(c.bounds)])
        assert len(pixels) == 9 * len(controls)
        accents = {"F2D46D", "FFE48A", "D9B94F"}
        for index, control in enumerate(controls):
            colours = pixels[9 * index:9 * (index + 1)]
            if index < len(branded):
                assert sum(pixel in accents for pixel in colours) >= 3, (control.label, colours)
            else:
                assert not accents.intersection(colours), (control.label, colours)

    def editor(copy=False, name=None):
        invoke(frame, "Styles Manager...")
        manager = dialog("Styles Manager")
        if name:
            select_style(manager, name)
        click(manager, "Copy" if copy else "Edit", rightmost=True)
        return manager, dialog("Style Editor")

    def close_manager(manager):
        wait(lambda: user.IsWindowEnabled(manager), "Styles Manager ready to close")
        assert user.PostMessageW(manager, 0x10, 0, 0)
        wait(lambda: not user.IsWindowVisible(manager), "Styles Manager closes")

    def property_dialog(parent, caption, title):
        click(parent, caption)
        return dialog(title)

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
    feature_captions = ("Furigana...", "Vertical...", "Multi-border...",
                        "Text alignment...", "Extended boxes...", "Gradient defaults...")
    expected = feature_captions + ("OK", "Cancel", "Apply", "Help")
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
    wrap = choice_control(style, lambda values: values == [
        "Inherit Script", "Smart", "End-of-line", "No automatic wrap", "Alternative smart"])
    branded, normal = controls[:len(feature_captions)], controls[len(feature_captions):]
    first_row = [c.bounds for c in branded[:3]]
    second_row = [c.bounds for c in branded[3:]]
    wrap_label = required(style, ("Wrap Mode",))[0].bounds
    wrap_bounds = rect(wrap)
    margins, alignment = required(style, ("Margins", "Alignment"))
    for button_row in (first_row, second_row):
        assert all(left[2] <= right[0] for left, right in zip(button_row, button_row[1:])), button_row
        assert max(bounds[1] for bounds in button_row) < min(bounds[3] for bounds in button_row), button_row
    assert first_row[0][0] == second_row[0][0] == wrap_label[0], "The extension rows share a left edge"
    assert max(margins.bounds[3], alignment.bounds[3]) <= min(bounds[1] for bounds in first_row)
    assert max(bounds[3] for bounds in first_row) <= min(bounds[1] for bounds in second_row)
    assert max(bounds[3] for bounds in second_row) <= min(wrap_label[1], wrap_bounds[1])
    assert wrap_label[2] <= wrap_bounds[0] and max(wrap_label[1], wrap_bounds[1]) < min(wrap_label[3], wrap_bounds[3])
    for action in normal:
        assert wrap_bounds[2] <= action.bounds[0], ("Wrap Mode belongs on the left", action.label)
        assert max(wrap_bounds[1], action.bounds[1]) < min(wrap_bounds[3], action.bounds[3]), (
            "Wrap Mode shares the action row", action.label)
    assert all(normalized_label(c.label) != normalized_label("Mangetsu Extensions...")
               for c in inventory(style)), "The original feature buttons are the direct entry points"
    for control in branded:
        hwnd = button(style, control.label)
        assert user.GetWindowLongW(hwnd, -16) & 0xF == 0xB, (control.label, "owner drawn")
    assert_native_palette(style, branded, normal)
    gradients = property_dialog(style, "Gradient defaults...", "Mangetsu Style Gradients")
    capture(gradients, "mangetsu-style-gradients")
    close(gradients, "OK")
    assert send(wrap, 0x147) == 3, "Existing q2 must be selected"  # CB_GETCURSEL
    for caption, title, fields in (("Furigana...", "Furigana", 9), ("Vertical...", "Vertical layout", 5),
                                   ("Multi-border...", "Multi-border", 7)):
        untouched = property_dialog(style, caption, title)
        assert_direct_form(untouched, fields)
        close(untouched, "OK")
    click_sync(style, "Apply")
    furi = property_dialog(style, "Furigana...", "Furigana")
    wait_row_value(furi, "Reading scale X (%)", "70")
    wait_row_value(furi, "Reading scale Y (%)", "70", enabled=False)
    assign(furi, "Reading scale X (%)", "80")
    wait_row_value(furi, "Reading scale Y (%)", "80", enabled=False)
    close(furi)
    furi = property_dialog(style, "Furigana...", "Furigana")
    wait_row_value(furi, "Reading scale X (%)", "70")
    reset(furi, "Reading scale X (%)")
    wait_row_value(furi, "Reading scale X (%)", "50")
    wait_row_value(furi, "Reading scale Y (%)", "50", enabled=False)
    assign(furi, "Reading scale X (%)", "80")
    capture(furi, "furigana")
    close(furi, "OK")
    close(style)
    close_manager(manager)
    document = lambda content: [line for line in content.splitlines() if line.startswith(("STYLE|", "LINE|"))]
    assert document(snapshot()) == document(replaced), "Untouched OK/Apply and parent Cancel preserve the document"

    manager, style = editor()
    alignment = property_dialog(style, "Text alignment...", "Mangetsu Style Alignment")
    click_sync(alignment, "Top left")
    assert button(alignment, "Top left (selected)"), "Selected alignment is exposed by the native button caption"
    capture(alignment, "mangetsu-style-alignment")
    close(alignment, "OK")
    furi = property_dialog(style, "Furigana...", "Furigana")
    assign(furi, "Reading scale X (%)", "80")
    close(furi, "OK")
    furi = property_dialog(style, "Furigana...", "Furigana")
    click_sync(furi, "Use the same reading scale on both axes")
    assign(furi, "Reading scale Y (%)", "90")
    wait_row_value(furi, "Reading scale X (%)", "80")
    close(furi, "OK")
    furi = property_dialog(style, "Furigana...", "Furigana")
    wait_row_value(furi, "Reading scale Y (%)", "90")
    reset(furi, "Reading scale Y (%)")
    wait_row_value(furi, "Reading scale Y (%)", "50")
    wait_row_value(furi, "Reading scale X (%)", "80")
    close(furi, "OK")
    vertical = property_dialog(style, "Vertical...", "Vertical layout")
    vert, _ = row(vertical, "Native vertical layout")
    assert send(vert, 0x147) == 0, "Inherited vertical layout starts disabled"
    assign(vertical, "Native vertical layout", 1, choice=True)
    assign(vertical, "Vertical unit policy", 3, choice=True)
    capture(vertical, "vertical")
    close(vertical, "OK")
    vertical = property_dialog(style, "Vertical...", "Vertical layout")
    reset(vertical, "Native vertical layout")
    vert, _ = row(vertical, "Native vertical layout")
    assert send(vert, 0x147) == 0
    assert send(row(vertical, "Vertical unit policy")[0], 0x147) == 3
    assign(vertical, "Native vertical layout", 1, choice=True)
    close(vertical, "OK")
    border = property_dialog(style, "Multi-border...", "Multi-border")
    required(border, ("Border alpha (0–255)",))
    wait_row_value(border, "Border size X", "2")
    for layer in range(2, 10):
        click_sync(border, str(layer))
        wait_row_value(border, "Border size X", "0")
        assign(border, "Border size X", str(layer))
        wait_row_value(border, "Border size Y", str(layer), enabled=False)
    click_sync(border, "10")
    wait_row_value(border, "Border size X", "0")
    click_sync(border, "2")
    wait_row_value(border, "Border size X", "2")
    reset(border, "Border size X")
    wait_row_value(border, "Border size X", "0")
    wait_row_value(border, "Border size Y", "0", enabled=False)
    assign(border, "Border size X", "2")
    click_sync(border, "10")
    wait_row_value(border, "Border size X", "0")
    assign(border, "Border size X", "-1")
    click(border, "9")
    close(dialog("Invalid style value"), "OK")
    wait_row_value(border, "Border size X", "-1")
    click(border, "OK")
    close(dialog("Invalid style value"), "OK")
    assert user.IsWindowVisible(border), "Invalid numbers must not close or commit the editor"
    assign(border, "Border size X", "3")
    capture(border, "multi-border")
    close(border, "OK")
    box = property_dialog(style, "Extended boxes...", "Extended boxes")
    assign(box, "Rectangular box", 1, choice=True)
    assign(box, "Corner radius", "6")
    capture(box, "mangetsu-extended-boxes")
    close(box, "OK")
    wrap = choice_control(style, lambda values: "Inherit Script" in values)
    choose(wrap, 1)  # q0; metadata only, not Script Info WrapStyle
    click_sync(style, "Apply")
    assert user.IsWindowVisible(style), "Apply leaves Style Editor open"
    wrap = choice_control(style, lambda values: "Inherit Script" in values)
    choose(wrap, 0)  # Pending reset must be discarded by Cancel after Apply.
    close(style)
    close_manager(manager)
    committed = snapshot()
    for tag in ("\\ta7", "\\box1", "\\boxr6", "\\furisx80", "\\vert1", "\\vtype3", "\\10bs3", "\\q0", "\\grd(45,", "\\vc(",
                *(f"\\{layer}bs{layer}" for layer in range(2, 10))):
        assert tag in committed, (tag, committed)
    assert "\\furisy" not in committed and "\\1bs" not in committed and "\\q2" not in committed
    assert "LINE|mangetsu-colorcoding|Default|{\\1c&HFFFFFF&}" in committed

    def history(prefix):
        # wx updates dynamic captions when the Edit menu opens. The stored ID
        # also works when the menu still displays its initial disabled caption.
        matches = [(label, identifier) for label, identifier in entries(user.GetMenu(frame))
                   if label.startswith(prefix) or label.casefold() == f"nothing to {prefix.lower()}"]
        assert len(matches) == 1, matches
        command_id(matches[0][1])

    def set_style_name(style, value):
        left, top, right, bottom = required(style, ("Style Name",))[0].bounds
        matches = [edit for edit in controls_of_kind(style, "edit")
                   if left <= edit.bounds[0] < edit.bounds[2] <= right
                   and top <= edit.bounds[1] < edit.bounds[3] <= bottom]
        assert len(matches) == 1, ("Style Name field", matches)
        set_text(matches[0].hwnd, value)
        assert get_text(matches[0].hwnd) == value, "Style Name edit was accepted"

    history("Undo")
    undone = snapshot()
    assert "\\furis70" in undone and "\\ta7" not in undone, "One undo restores style metadata"
    history("Redo")
    assert "\\furisx80" in snapshot()
    manager, style = editor(copy=True)
    set_style_name(style, "UI Copy")
    close(style, "OK")
    close_manager(manager)
    copied = snapshot()
    assert "STYLE|UI Copy" in copied, copied
    assert "LINE|mangetsu-style|UI Copy|" in copied, copied
    invoke(frame, "Styles Manager...")
    manager = dialog("Styles Manager")
    select_style(manager, "UI Copy")
    click(manager, "Delete", rightmost=True)
    confirm = dialog("Confirm delete from current")
    close(confirm, "Yes")
    close_manager(manager)
    assert "LINE|mangetsu-style|UI Copy|" not in snapshot(), "Delete removes only copied declarations"

    manager, style = editor()
    set_style_name(style, "UI Sign")
    click(style, "OK")
    close(dialog("Update script?"), "Yes")
    wait(lambda: not user.IsWindowVisible(style), "renamed Style Editor closes")
    close_manager(manager)
    renamed = snapshot()
    assert "LINE|mangetsu-style|UI Sign|" in renamed
    command("Save Subtitles")
    wait(lambda: "\\furisx80" in (profile / "style-ui.ass").read_text(encoding="utf-8-sig"), "committed metadata saved")
    saved = (profile / "style-ui.ass").read_text(encoding="utf-8-sig")
    assert "WrapStyle: 0" in saved and "mangetsu-colorcoding" in saved

    def choose_subtitle_file(path):
        picker = dialog("Open subtitles file")

        def filename_edit():
            edits = [control for control in inventory(picker)
                     if control.kind.lower() == "edit" and enabled_in_dialog(control.hwnd, picker)]
            for control in edits:
                ancestor = control.hwnd
                while ancestor and ancestor != picker:
                    if (user.GetDlgCtrlID(ancestor) == 1148  # Win32 file-name combo (cmb13)
                            or text(ancestor, True) == "FileNameControlHost"):
                        return control.hwnd
                    ancestor = user.GetParent(ancestor)
            return edits[0].hwnd if len(edits) == 1 else None

        set_text(wait(filename_edit, "native file-name editor"), str(path))
        close(picker, "Open")

    reopened = profile / "style-ui-reopened.ass"
    reopened.write_bytes((profile / "style-ui.ass").read_bytes())
    invoke(frame, "Open Subtitles...")
    choose_subtitle_file(reopened)
    wait(lambda: reopened.stem in text(frame), "reopened ASS becomes the active document")
    reloaded = snapshot()
    assert document(reloaded) == document(renamed), "Reopening preserves the saved styles and metadata"

    def select_renderer(name):
        invoke(frame, "Options...")
        preferences = dialog("Preferences")
        renderer = choice_control(preferences, lambda values: {"Mangetsu", "libass"}.issubset(values))
        choose(renderer, choices(renderer).index(name))
        close(preferences, "OK")

    select_renderer("libass")
    wait(lambda: not any(name == "Show Mangetsu Metadata" for name, _ in entries(user.GetMenu(frame))), "renderer switch hides metadata menu")
    manager, style = editor(name="UI Sign")
    capture(style, "style-editor-libass")
    alignment = property_dialog(style, "Text alignment...", "Mangetsu Style Alignment")
    required(alignment, ("Logical line alignment",))
    capture(alignment, "mangetsu-style-alignment-libass")
    close(alignment)
    close(style)
    close_manager(manager)
    assert "\\furisx80" in snapshot(), "Renderer switching preserves saved metadata"
    select_renderer("Mangetsu")
    manager, style = editor(name="UI Sign")
    furi = property_dialog(style, "Furigana...", "Furigana")
    wait_row_value(furi, "Reading scale X (%)", "80")
    wait_row_value(furi, "Reading scale Y (%)", "50")
    close(furi)
    close(style)
    close_manager(manager)
    command("Select first dialogue")
    invoke(frame, "Mangetsu Text Alignment...")
    alignment = dialog("Mangetsu Text Alignment")
    click_sync(alignment, "Bottom right")
    capture(alignment, "mangetsu-dialogue-alignment")
    close(alignment, "OK")
    assert "LINE||UI Sign|{\\ta3}" in snapshot(), "Inline alignment updates the active dialogue"
    history("Undo")
    assert document(snapshot()) == document(reloaded), "Inline alignment is a single undo step"
    command("Select first dialogue")
    for caption in ("Gradient", "Mangetsu Chat Style"):
        hwnd = button(frame, caption)
        assert user.GetWindowLongW(hwnd, -16) & 0xF == 0xB, (caption, "owner drawn")
    click(frame, "Gradient")
    gradient_editor = wait(lambda: next((h for h in windows(pid) if text(h).startswith("Gradient Editor")
                                        and user.IsWindowVisible(h)), None), "dialogue gradient editor")
    width = controls_of_kind(gradient_editor, "edit")
    slide_label = required(gradient_editor, ("Initial gradient slide width",))[0]
    matching = [c for c in width if c.bounds[0] >= slide_label.bounds[2]
                and abs(sum(c.bounds[1::2]) - sum(slide_label.bounds[1::2])) < 16]
    assert len(matching) == 1, "The slide-width input is beside its label"
    set_text(matching[0].hwnd, "16")
    close(gradient_editor, "OK")
    assert "LINE||UI Sign|{\\grds16}" in snapshot(), "The gradient editor stores the initial slide width"
    history("Undo")
    assert document(snapshot()) == document(reloaded), "Slide width is a single undo step"

    source = profile / "inheritance-source.ass"
    style_record = next(line for line in saved.splitlines() if line.startswith("Style: "))
    event_format = next(line for line in saved.splitlines() if line.startswith("Format: Layer,"))
    style_format = next(line for line in saved.splitlines() if line.startswith("Format: Name,"))
    source.write_text("\n".join((
        "[Script Info]", "ScriptType: v4.00+", "PlayResX: 320", "PlayResY: 180",
        "[V4+ Styles]", style_format, "Style: Reusable," + style_record.split(",", 1)[1],
        "[Events]", event_format,
        r"Comment: 0,0:00:00.00,0:00:00.00,Reusable,Bob,0,0,0,mangetsu-colorcoding{note},{\1c&H123456&}",
        r"Comment: 0,0:00:00.00,0:00:00.00,Reusable,,0,0,0,{uketsugu}mangetsu-style,{\furis90}",
        "Comment: 0,0:00:00.00,0:00:00.00,Reusable,,0,0,0,prefix{uketsugu},inherited comment",
        "Dialogue: 2,0:00:00.00,0:00:02.00,Reusable,Bob,1,2,3,{uketsugu}suffix,inherited dialogue",
        "Dialogue: 0,0:00:00.00,0:00:02.00,Reusable,,0,0,0,{uketsugu2},unmarked dialogue",
        "",
    )), encoding="utf-8")
    invoke(frame, "Styles Manager...")
    manager = dialog("Styles Manager")
    click(manager, "Import from script...")
    choose_subtitle_file(source)
    chooser = dialog("Import Styles")
    click_sync(chooser, "Select All")
    close(chooser, "OK")
    close_manager(manager)
    imported = document(snapshot())
    assert "STYLE|Reusable" in imported
    events = [line for line in imported if line.startswith("LINE|")]
    expected_new = [
        r"LINE|{uketsugu}mangetsu-style|Reusable|{\furis90}",
        r"LINE|mangetsu-colorcoding{note}|Reusable|{\1c&H123456&}",
        "LINE|prefix{uketsugu}|Reusable|inherited comment",
        "LINE|{uketsugu}suffix|Reusable|inherited dialogue",
    ]
    positions = [events.index(line) for line in expected_new]
    old_events = [line for line in reloaded.splitlines() if line.startswith("LINE|")]
    assert [line for line in events if line not in expected_new] == old_events
    assert positions == sorted(positions) and positions[1:4] == list(range(positions[1], positions[1] + 3))
    assert positions[-1] < events.index(next(line for line in old_events if line.startswith("LINE|note|")))
    assert all("unmarked dialogue" not in line for line in events)
    history("Undo")
    assert document(snapshot()) == document(reloaded), "Style import and inherited events share one undo step"
    history("Redo")
    assert document(snapshot()) == imported, "Redo restores the complete inherited block"
    command("Save Subtitles")
    wait(lambda: "inherited dialogue" in reopened.read_text(encoding="utf-8-sig"), "inherited events saved")
    round_trip = profile / "inherited-reopened.ass"
    round_trip.write_bytes(reopened.read_bytes())
    invoke(frame, "Open Subtitles...")
    choose_subtitle_file(round_trip)
    wait(lambda: round_trip.stem in text(frame), "inherited ASS reopens")
    assert document(snapshot()) == imported, "Save and reload retain annotations and event order"

    bracket_source = profile / "bracket-picker.ass"
    raw = r"{\an7}မြန်မာ\N漢字 かんじ"
    bracket_source.write_text("\n".join((
        "[Script Info]", "ScriptType: v4.00+", "PlayResX: 320", "PlayResY: 180",
        "[V4+ Styles]", style_format, "Style: Default," + style_record.split(",", 1)[1],
        "[Events]", event_format, "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,," + raw, "",
    )), encoding="utf-8")
    invoke(frame, "Open Subtitles...")
    choose_subtitle_file(bracket_source)
    wait(lambda: bracket_source.stem in text(frame), "bracket fixture opens")
    editors = controls_of_kind(frame, "scintilla", count=1)
    edit = editors[0].hwnd
    exercise_brackets(edit, raw, user, wait, send, dialog, controls_of_kind, snapshot, history)
    command("Save Subtitles")
    wait(lambda: "<漢字|かんじ>" in bracket_source.read_text(encoding="utf-8-sig"), "bracket fixture saved")
    (profile / "style-ui-result.txt").write_text("metadata visibility, direct edit/reset, ten layers, validation, transactions, copy/delete/rename, inheritance, undo/redo, save/reload, renderer switching, bracket picker: passed\n", encoding="utf-8")


def exercise_brackets(edit, raw, user, wait, send, dialog, controls_of_kind, snapshot, history):
    class GuiThreadInfo(C.Structure):
        _fields_ = [("cbSize", W.DWORD), ("flags", W.DWORD)] + [
            (name, W.HWND) for name in ("active", "focus", "capture", "menu", "move_size", "caret")
        ] + [("caret_rect", W.RECT)]

    user.GetGUIThreadInfo.argtypes = (W.DWORD, C.POINTER(GuiThreadInfo))
    user.GetGUIThreadInfo.restype = W.BOOL
    thread = user.GetWindowThreadProcessId(edit, None)

    def expect_focus():
        info = GuiThreadInfo(C.sizeof(GuiThreadInfo))
        assert user.GetGUIThreadInfo(thread, C.byref(info)) and info.focus == edit

    def key(hwnd, vk):
        assert user.PostMessageW(hwnd, 0x100, vk, 1)  # WM_KEYDOWN

    def begin(expected_index):
        key(edit, 0x77)  # F8, manually configured for this profile's Subtitle Edit Box context
        picker = dialog("Brackets and Furigana")
        items = controls_of_kind(picker, "listbox", count=1)[0].hwnd
        assert send(items, 0x18B) == 14  # LB_GETCOUNT
        assert send(items, 0x188) == expected_index  # LB_GETCURSEL
        return picker, items

    def select(items, vk, count, index):
        key(items, vk)
        wait(lambda: send(items, 0x18B) == count and send(items, 0x188) == index,
             f"picker page has {count} rows and selection {index}")

    def finish(picker, items, vk=0x0D):
        key(items, vk)
        wait(lambda: not user.IsWindowVisible(picker), "bracket picker closes")
        wait(lambda: user.IsWindowEnabled(edit), "subtitle editor is enabled")

    def expect_text(value):
        actual = snapshot().splitlines()
        assert "LINE||Default|" + value in actual, actual

    def position(prefix):
        return len(prefix.encode("utf-8"))

    def select_text(start, end):
        send(edit, 2160, start, end)  # SCI_SETSEL

    def expect_caret(offset):
        assert send(edit, 2008) == send(edit, 2009) == offset  # SCI_GETCURRENTPOS/SCI_GETANCHOR
        expect_focus()

    shown = raw.replace(r"\N", "\n")
    assert send(edit, 2183) == position(shown), "Better View shows a visual line break"
    prefix = r"{\an7}"
    start = position(prefix)
    select_text(start, position(shown))
    picker, items = begin(1)
    select(items, 0x26, 14, 0)  # Up
    select(items, 0x28, 14, 1)  # Down
    select(items, 0x26, 14, 0)
    select(items, 0x25, 2, 0)  # Left
    select(items, 0x27, 14, 0)  # Right restores the bracket choice
    finish(picker, items)
    wrapped = prefix + "「" + raw[len(prefix):] + "」"
    expect_caret(position(shown + "「"))
    expect_text(wrapped)
    history("Undo")
    expect_text(raw)
    history("Redo")
    expect_text(wrapped)
    history("Undo")

    select_text(start, start)
    picker, items = begin(0)  # Last choice was retained.
    finish(picker, items, 0x1B)
    assert send(edit, 2008) == send(edit, 2009) == start
    expect_text(raw)
    # Escape creates no history entry. The last insertion is still redoable.
    history("Redo")
    expect_text(wrapped)
    history("Undo")

    select_text(start, start)
    picker, items = begin(0)
    finish(picker, items)
    expect_caret(start + position("「"))
    expect_text(prefix + "「」" + raw[len(prefix):])
    history("Undo")

    for reverse in (False, True):
        select_text(start, start)
        picker, items = begin(0)
        select(items, 0x25, 2, 0)
        if reverse:
            select(items, 0x28, 2, 1)
        finish(picker, items)
        expect_caret(start + (1 if reverse else 2))
        expect_text(prefix + "<|>" + raw[len(prefix):])
        history("Undo")

    base_at = position(shown.split("漢字")[0])
    reading_at = position(shown.split("かんじ")[0])
    select_text(reading_at, reading_at + position("かんじ"))
    picker, items = begin(0)
    select(items, 0x25, 2, 0)
    select(items, 0x28, 2, 1)
    finish(picker, items)
    expect_caret(reading_at + 1)
    expect_text(raw.replace("かんじ", "<|かんじ>"))
    history("Undo")

    select_text(base_at, base_at + position("漢字"))
    picker, items = begin(0)
    select(items, 0x25, 2, 0)
    finish(picker, items)
    expect_caret(base_at + position("<漢字|"))
    expect_text(raw.replace("漢字", "<漢字|>"))
    for char in "かんじ":
        assert user.PostMessageW(edit, 0x102, ord(char), 1)  # WM_CHAR
    wait(lambda: send(edit, 2183) == position(shown.replace("漢字", "<漢字|かんじ>")), "reading typed at caret")
    expect_text(raw.replace("漢字", "<漢字|かんじ>"))
