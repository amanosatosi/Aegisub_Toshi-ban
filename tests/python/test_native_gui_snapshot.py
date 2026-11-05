"""Headless lifecycle regressions; the packaged Windows suite tests real HWNDs."""
import unittest
from native_gui_snapshot import Control, StaleWindow, control_snapshot, editor_row, enable_editor, notification_wparam


def normalize(text):
    return text.replace("&", "").casefold()


class NativeControlSnapshots(unittest.TestCase):
    def setUp(self):
        self.controls = {
            1: Control(1, "Border size X", "Static", (100, 10, 200, 30), True),
            2: Control(2, "&Override", "Button", (10, 10, 90, 30), True),
            3: Control(3, "", "Edit", (210, 10, 300, 30), False),
        }

    def test_label_destroyed_during_geometry_read_discards_generation(self):
        def describe(hwnd):
            if hwnd == 1:
                raise StaleWindow("GetWindowRect: invalid HWND")
            return self.controls[hwnd]
        self.assertIsNone(control_snapshot([1, 2, 3], describe, lambda c: True))
        fresh = control_snapshot([1, 2, 3], self.controls.get, lambda c: True)
        self.assertEqual(editor_row(fresh, "Border size X", normalize), (2, 3))

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
        self.assertEqual(editor_row(tuple(self.controls.values()), "Border size X", normalize), (2, 3))

    def test_hidden_controls_are_excluded_without_mixing_generations(self):
        snapshot = control_snapshot([1, 2, 3, 4], self.controls.get, lambda c: True)
        self.assertEqual(len(snapshot), 3)
        self.assertEqual(editor_row(snapshot, "Border size X", normalize), (2, 3))

    def test_discovery_errors_other_than_staleness_are_not_swallowed(self):
        def failed(hwnd):
            raise RuntimeError("GUI stopped responding")
        with self.assertRaisesRegex(RuntimeError, "GUI stopped responding"):
            control_snapshot([1], failed, lambda c: True)

    def test_adjacent_row_and_pair_do_not_select_the_wrong_editor(self):
        controls = tuple(self.controls.values()) + (
            Control(4, "Border size Y", "Static", (100, 40, 200, 60), True),
            Control(5, "Override", "Button", (10, 40, 90, 60), True),
            Control(6, "", "Edit", (210, 40, 300, 60), True),
            Control(7, "", "Edit", (310, 40, 400, 60), True),
        )
        self.assertEqual(editor_row(controls, "Border size Y", normalize), (5, 6))

    def test_checked_owner_drawn_override_is_not_toggled_off(self):
        # wx stores this checkbox state internally; native BM_GETCHECK may be
        # zero even though its editor is already enabled.
        clicks = []
        self.assertEqual(enable_editor(lambda: (2, 3), lambda h: True, clicks.append), 3)
        self.assertEqual(clicks, [])

    def test_inherited_editor_is_enabled_by_exactly_one_click(self):
        enabled = {2: True, 3: False}
        clicks = []
        def click(hwnd):
            clicks.append(hwnd)
            enabled[3] = not enabled[3]
        self.assertEqual(enable_editor(lambda: (2, 3), enabled.get, click), 3)
        self.assertEqual(clicks, [2])

    def test_override_callback_reacquires_a_rebuilt_editor(self):
        current = [2, 3]
        enabled = {2: True, 3: False, 4: True, 5: True}
        def click(hwnd):
            current[:] = (4, 5)
        self.assertEqual(enable_editor(lambda: tuple(current), enabled.get, click), 5)

    def test_linked_axis_cannot_be_enabled_by_synthetic_click(self):
        clicks = []
        with self.assertRaisesRegex(AssertionError, "unlink"):
            enable_editor(lambda: (2, 3), lambda h: False, clicks.append)
        self.assertEqual(clicks, [])

    def test_failed_override_handler_is_reported_without_replaying_click(self):
        clicks = []
        with self.assertRaisesRegex(AssertionError, "did not enable"):
            enable_editor(lambda: (2, 3), lambda h: h == 2, clicks.append)
        self.assertEqual(clicks, [2])

    def test_negative_wx_control_id_does_not_overwrite_notification_code(self):
        for identifier in (-2001, -32700, 1024):
            for code in (0, 1, 5):
                packed = notification_wparam(identifier, code)
                self.assertEqual(packed & 0xFFFF, identifier & 0xFFFF)
                self.assertEqual(packed >> 16, code)


if __name__ == "__main__":
    unittest.main()
