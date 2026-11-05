"""Read-only native control discovery across a wxWidgets form rebuild.

Keep retryable discovery separate from actions: replaying a partially completed
click, Apply or menu command could change the document twice.
"""
from dataclasses import dataclass


class StaleWindow(RuntimeError):
    """A control disappeared or its HWND was reused during a read."""


def notification_wparam(identifier, notification):
    """MAKEWPARAM: wx auto-generated control IDs may be negative."""
    return (identifier & 0xFFFF) | ((notification & 0xFFFF) << 16)


def enable_editor(lookup, enabled, click):
    """Observe the editor, not BM_GETCHECK on owner-drawn wx checkboxes."""
    override, field = lookup()
    if not enabled(override):
        raise AssertionError("Override is unavailable (unlink the axes before editing this row)")
    if not enabled(field):
        click(override)
        override, field = lookup()
    if not enabled(field):
        raise AssertionError("Override callback did not enable its editor")
    return field


@dataclass(frozen=True)
class Control:
    hwnd: int
    label: str
    kind: str
    bounds: tuple
    enabled: bool
    identity: tuple = ()


def control_snapshot(handles, describe, still_current):
    """Discard the whole generation if any enumerated control becomes stale."""
    try:
        controls = tuple(c for h in handles if (c := describe(h)) is not None)
        if all(still_current(c) for c in controls):
            return controls
    except StaleWindow:
        pass
    return None


def editor_row(controls, caption, normalize):
    """Find label, Override and input in one snapshot, never by re-enumerating."""
    if controls is None:
        return None
    titles = [c for c in controls if normalize(c.label) == normalize(caption)]
    for title in titles:
        y = (title.bounds[1] + title.bounds[3]) / 2
        aligned = [c for c in controls if abs((c.bounds[1] + c.bounds[3]) / 2 - y) < 12]
        overrides = [c for c in aligned if normalize(c.label) == "override"]
        inputs = [c for c in aligned if c.kind.lower() in ("edit", "combobox")
                  and c.bounds[0] >= title.bounds[2]]
        if overrides and inputs:
            nearest = lambda c: abs((c.bounds[1] + c.bounds[3]) / 2 - y)
            return min(overrides, key=nearest).hwnd, min(inputs, key=lambda c: c.bounds[0]).hwnd
    return None
