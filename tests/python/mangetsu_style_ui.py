"""Real wxWidgets style/metadata transactions, run by packaged Windows smoke CI.

Only public UI and an ordinary Automation macro are used; no product test hooks.
"""
import ctypes as C
import os
import subprocess
import time
import unicodedata


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
""", encoding="utf-8")
    return fixture


def exercise(profile, pid, frame, user, windows, text, wait, send, entries, invoke, rect):
    def label(h):
        return text(h).replace("&", "")

    def normalized_label(value):
        # Win32/wx can display native mnemonic markers and different
        # ellipsis glyphs while keeping the same user-visible command.
        return unicodedata.normalize("NFKC", value.replace("&", "")).replace("…", "...").strip().casefold()

    def children(parent):
        return [h for h in windows(pid, parent) if user.IsWindowVisible(h)]

    def named(parent, caption):
        target = normalized_label(caption)
        return [h for h in children(parent) if normalized_label(label(h)) == target]

    def click(parent, caption, rightmost=False):
        handles = wait(lambda: named(parent, caption), caption + " control")
        h = max(handles, key=lambda w: rect(w)[0]) if rightmost else handles[0]
        assert user.IsWindowEnabled(h), caption + " must be enabled"
        user.PostMessageW(h, 0xF5, 0, 0)  # BM_CLICK (async, may enter a modal loop)

    def click_sync(parent, caption):
        # Only for non-modal handlers such as Apply. Unlike BM_CLICK posted
        # asynchronously, SendMessageTimeout waits for the native handler.
        handles = wait(lambda: named(parent, caption), caption + " control")
        h = handles[0]
        assert user.IsWindowEnabled(h), caption + " must be enabled"
        result = C.c_size_t()
        assert user.SendMessageTimeoutW(h, 0xF5, 0, 0, 2, 30000, C.byref(result)), "Native Apply handler timed out"

    def dialog(caption):
        return wait(lambda: next((h for h in windows(pid) if text(h) == caption and user.IsWindowVisible(h)), None), caption)

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

    def choose(h, index):
        send(h, 0x14E, index)  # CB_SETCURSEL
        # Notify the owning dialog synchronously. Asynchronous selection
        # events otherwise race the immediately following Apply/Cancel.
        result = C.c_size_t()
        assert user.SendMessageTimeoutW(user.GetParent(h), 0x111, user.GetDlgCtrlID(h) | (1 << 16), h, 2, 30000, C.byref(result)), "Native choice event timed out"

    def row(parent, caption):
        # Switching the border layer destroys and recreates the whole native
        # form. A label may appear before its checkbox/input: treat the trio
        # as one atomic readiness condition, not three separate lookups.
        def complete_row():
            visible = children(parent)
            for title in named(parent, caption):
                y = sum(rect(title)[1::2]) / 2
                aligned = [h for h in visible if abs(sum(rect(h)[1::2]) / 2 - y) < 12]
                overrides = [h for h in aligned if normalized_label(label(h)) == "override"]
                inputs = [h for h in aligned if text(h, True).lower() in ("edit", "combobox")]
                if overrides and inputs:
                    nearest = lambda h: abs(sum(rect(h)[1::2]) / 2 - y)
                    return min(overrides, key=nearest), min(inputs, key=lambda h: rect(h)[0])
            return None

        return wait(complete_row, caption + " complete native editor row")

    def assign(parent, caption, value, choice=False):
        override, field = row(parent, caption)
        if not send(override, 0xF0):  # BM_GETCHECK
            user.PostMessageW(override, 0xF5, 0, 0)
            wait(lambda: user.IsWindowEnabled(field), caption + " override enabled")
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
        user.PostMessageW(manager, 0x10, 0, 0)
        wait(lambda: not user.IsWindowVisible(manager), "Styles Manager closes")

    def select_style(manager, name):
        lists = [h for h in children(manager) if text(h, True).lower() == "listbox"]
        current = max(lists, key=lambda h: rect(h)[0])
        for index in range(send(current, 0x18B)):  # LB_GETCOUNT
            buf = C.create_unicode_buffer(2048)
            send(current, 0x189, index, C.addressof(buf))  # LB_GETTEXT
            if buf.value == name:
                send(current, 0x185, 0, -1)  # LB_SETSEL: clear, then select
                send(current, 0x185, 1, index)
                user.PostMessageW(user.GetParent(current), 0x111, user.GetDlgCtrlID(current) | (1 << 16), current)
                return
        raise AssertionError("Style missing: " + name)

    def command_id(identifier):
        # These commands require no user input. Wait for the complete native
        # handler, including LuaThreadedCall's modal loop and selection update.
        # A file written inside a macro is not a completion notification: posting
        # another WM_COMMAND then can bypass the disabled frame and reenter Lua.
        wait(lambda: user.IsWindowEnabled(frame), "main frame ready for command")
        result = C.c_size_t()
        assert user.SendMessageTimeoutW(frame, 0x111, identifier, 0, 2, 30000, C.byref(result)), "Native command failed or application exited"

    def command(caption):
        matches = [identifier for name, identifier in entries(user.GetMenu(frame)) if name == caption]
        assert len(matches) == 1, (caption, matches)
        command_id(matches[0])

    initial = snapshot()
    assert "SELECTED|mangetsu-" not in initial and "ACTIVE|mangetsu-" not in initial
    assert sum(line.startswith("LINE|mangetsu-") for line in initial.splitlines()) == 3
    command("Show Mangetsu Metadata")
    command("Select style metadata")
    assert "ACTIVE|mangetsu-style" in snapshot()
    command("Show Mangetsu Metadata")
    hidden = snapshot()
    assert "ACTIVE|mangetsu-" not in hidden and "SELECTED|mangetsu-" not in hidden
    assert [s for s in initial.splitlines() if s.startswith("LINE|")] == [s for s in hidden.splitlines() if s.startswith("LINE|")]
    sorts = [command for name, command in entries(user.GetMenu(frame)) if name == "Actor Name"]
    assert len(sorts) == 2
    command_id(sorts[0])  # Sort all; preserve the ordered metadata header.
    ordered = snapshot()
    assert [s for s in ordered.splitlines() if s.startswith("LINE|")][:3] == [s for s in initial.splitlines() if s.startswith("LINE|")][:3]
    invoke(frame, "Find and Replace...")
    replace = dialog("Replace")
    fields = sorted([h for h in children(replace) if "combobox" in text(h, True).lower()], key=lambda h: rect(h)[1])
    assert len(fields) == 2
    set_text(fields[0], "70")
    set_text(fields[1], "71")
    click(replace, "Replace all")
    message = wait(lambda: next((h for h in windows(pid) if h not in (frame, replace) and user.IsWindowVisible(h) and named(h, "OK")), None), "replacement result")
    close(message, "OK")
    close(replace)
    replaced = snapshot()
    assert "Ordinary comment 71" in replaced, "Ordinary comments remain editable"
    assert "\\furis70" in replaced and "\\furis71" not in replaced, "Hidden metadata is excluded from Replace All"

    manager, style = editor()
    # wxWidgets may publish a native dialog window before all children have
    # finished showing. Wait for its actual controls, but never ignore an
    # absent button: report which one is missing and retain a UI screenshot.
    expected = ("Furigana…", "Vertical…", "Multi-border…", "OK", "Cancel", "Apply", "Help")
    try:
        controls = wait(
            lambda: [named(style, n)[0] for n in expected] if all(named(style, n) for n in expected) else None,
            "all Style Editor bottom-row controls",
            seconds=10,
        )
    except AssertionError as exc:
        inventory = [(label(h), text(h, True), user.IsWindowVisible(h), rect(h))
                     for h in windows(pid, style)]
        missing = [n for n in expected if not named(style, n)]
        diagnostic = profile / "style-ui-result.txt"
        diagnostic.write_text(
            "Missing Style Editor controls: " + repr(missing) + "\\n"
            + "All child HWNDs (caption, class, visible, rect):\\n"
            + "\\n".join(map(repr, inventory)) + "\\n",
            encoding="utf-8",
        )
        try:
            capture(style, "style-editor-missing-controls")
        except Exception as capture_error:
            diagnostic.write_text(diagnostic.read_text(encoding="utf-8")
                                  + "Screenshot failed: " + repr(capture_error) + "\n", encoding="utf-8")
        raise AssertionError(f"Style Editor is missing visible controls {missing}; "
                             f"native child inventory: {inventory}") from exc
    wrap = wait(lambda: next((h for h in children(style) if choices(h) == ["Inherit Script", "Smart", "End-of-line", "No automatic wrap", "Alternative smart"]), None), "Wrap Mode native choice")
    centers = [sum(rect(h)[1::2]) / 2 for h in controls + [wrap]]
    assert max(centers) - min(centers) < 5, "All eight controls must share one bottom row"
    assert rect(controls[2])[2] <= rect(wrap)[0] < min(rect(h)[0] for h in controls[3:]), "Wrap belongs to left group"
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
    assert named(border, "Border alpha (0–255)"), "The en dash in Border alpha must render correctly on Windows"
    click(border, "10")
    wait(lambda: get_text(row(border, "Border size X")[1]) == "0" and not send(row(border, "Border size X")[0], 0xF0), "layer ten form finishes switching")
    assign(border, "Border size X", "-1")
    click(border, "OK")
    close(dialog("Invalid style value"), "OK")
    assert user.IsWindowVisible(border), "Invalid numbers must not close or commit the editor"
    assign(border, "Border size X", "3")
    capture(border, "multi-border")
    close(border, "OK")
    wrap = next(h for h in children(style) if "Inherit Script" in choices(h))
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
    edits = [h for h in children(style) if text(h, True).lower() == "edit"]
    set_text(min(edits, key=lambda h: (rect(h)[1], rect(h)[0])), "UI Copy")
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
    edits = [h for h in children(style) if text(h, True).lower() == "edit"]
    set_text(min(edits, key=lambda h: (rect(h)[1], rect(h)[0])), "UI Sign")
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
    renderer = wait(lambda: next((h for h in children(preferences) if {"Mangetsu", "libass"}.issubset(choices(h))), None), "renderer dropdown")
    choose(renderer, choices(renderer).index("libass"))
    close(preferences, "OK")
    wait(lambda: not any(name == "Show Mangetsu Metadata" for name, _ in entries(user.GetMenu(frame))), "renderer switch hides metadata menu")
    manager, style = editor()
    assert not any(named(style, name) for name in ("Furigana…", "Vertical…", "Multi-border…", "Wrap Mode"))
    capture(style, "style-editor-libass")
    close(style)
    close_manager(manager)
    assert "\\furis80" in snapshot(), "Renderer switching preserves saved metadata"
    (profile / "style-ui-result.txt").write_text("metadata visibility, sort, replace, transactions, layout, copy/delete/rename, undo/redo, save, renderer switch: passed\n", encoding="utf-8")
