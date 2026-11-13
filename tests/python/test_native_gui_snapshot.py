"""Headless lifecycle regressions; the packaged Windows suite tests real HWNDs."""
import unittest
from native_gui_snapshot import Control, StaleWindow, control_snapshot, editor_row, notification_wparam


def normalize(text):
    return text.replace("&", "").casefold()


class NativeControlSnapshots(unittest.TestCase):
    def setUp(self):
        self.controls = {
            1: Control(1, "Border size X", "Static", (100, 10, 200, 30), True),
            2: Control(2, "&Reset", "Button", (420, 10, 500, 30), True),
            3: Control(3, "", "Edit", (210, 10, 300, 30), True),
        }

    def test_label_destroyed_during_geometry_read_discards_generation(self):
        def describe(hwnd):
            if hwnd == 1:
                raise StaleWindow("GetWindowRect: invalid HWND")
            return self.controls[hwnd]
        self.assertIsNone(control_snapshot([1, 2, 3], describe, lambda c: True))
        fresh = control_snapshot([1, 2, 3], self.controls.get, lambda c: True)
        self.assertEqual(editor_row(fresh, "Border size X", normalize), (3, 2))

    def test_input_destroyed_after_read_does_not_escape_snapshot(self):
        self.assertIsNone(control_snapshot([1, 2, 3], self.controls.get, lambda c: c.hwnd != 3))

    def test_control_id_detects_reuse_with_the_same_caption_and_class(self):
        original = Control(3, "", "Edit", (210, 10, 300, 30), True, (42, 100))
        replacement = Control(3, "", "Edit", original.bounds, True, (42, 101))
        self.assertIsNone(control_snapshot([3], lambda h: original, lambda c: c.identity == replacement.identity))

    def test_layout_change_discards_geometry_before_row_lookup(self):
        moved = Control(1, "Border size X", "Static", (100, 50, 200, 70), True)
        self.assertIsNone(control_snapshot([1, 2, 3], self.controls.get,
                                          lambda c: c.hwnd != 1 or c.bounds == moved.bounds))

    def test_reused_handle_is_rejected_even_when_it_still_exists(self):
        original = self.controls[3]
        def describe(hwnd):
            result = self.controls[hwnd]
            if hwnd == 3:
                self.controls[hwnd] = Control(3, "Other dialog", "Button", result.bounds, True)
            return result
        self.assertIsNone(control_snapshot([1, 2, 3], describe, lambda c: c == self.controls[c.hwnd]))
        self.assertNotEqual(original, self.controls[3])

    def test_incomplete_row_waits_for_all_three_controls(self):
        self.assertIsNone(editor_row(tuple(self.controls.values())[:2], "Border size X", normalize))
        self.assertEqual(editor_row(tuple(self.controls.values()), "Border size X", normalize), (3, 2))

    def test_hidden_controls_are_excluded_without_mixing_generations(self):
        snapshot = control_snapshot([1, 2, 3, 4], self.controls.get, lambda c: True)
        self.assertEqual(len(snapshot), 3)
        self.assertEqual(editor_row(snapshot, "Border size X", normalize), (3, 2))

    def test_discovery_errors_other_than_staleness_are_not_swallowed(self):
        def failed(hwnd):
            raise RuntimeError("GUI stopped responding")
        with self.assertRaisesRegex(RuntimeError, "GUI stopped responding"):
            control_snapshot([1], failed, lambda c: True)

    def test_adjacent_row_and_pair_do_not_select_the_wrong_editor(self):
        controls = tuple(self.controls.values()) + (
            Control(4, "Border size Y", "Static", (100, 40, 200, 60), True),
            Control(5, "Reset", "Button", (420, 40, 500, 60), True),
            Control(6, "", "Edit", (210, 40, 300, 60), True),
            Control(7, "", "Edit", (310, 40, 400, 60), True),
        )
        self.assertEqual(editor_row(controls, "Border size Y", normalize), (6, 5))

    def test_linked_axis_can_be_discovered_while_editor_and_reset_are_disabled(self):
        for hwnd in (2, 3):
            control = self.controls[hwnd]
            self.controls[hwnd] = Control(hwnd, control.label, control.kind, control.bounds, False)
        self.assertEqual(editor_row(tuple(self.controls.values()), "Border size X", normalize), (3, 2))

    def test_reset_from_neighboring_row_cannot_complete_incomplete_row(self):
        self.controls[2] = Control(2, "Reset", "Button", (420, 40, 500, 60), True)
        self.assertIsNone(editor_row(tuple(self.controls.values()), "Border size X", normalize))

    def test_negative_wx_control_id_does_not_overwrite_notification_code(self):
        for identifier in (-2001, -32700, 1024):
            for code in (0, 1, 5):
                packed = notification_wparam(identifier, code)
                self.assertEqual(packed & 0xFFFF, identifier & 0xFFFF)
                self.assertEqual(packed >> 16, code)


if __name__ == "__main__":
    unittest.main()
