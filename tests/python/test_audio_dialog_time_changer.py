"""CI source contracts for Shift+drag dialogue timing with both neighbors."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DialogTimeChangerContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/audio_timing_dialogue.cpp").read_text(encoding="utf-8")
        cls.method = source.split("bool AudioTimingControllerDialogue::ApplyDialogTimeChanger(", 1)[1].split(
            "\nvoid AudioTimingControllerDialogue::UpdateSelection()", 1
        )[0]

    def test_adjacent_lines_are_both_eligible(self):
        body = self.method
        self.assertIn("line_it != context->ass->Events.begin()", body)
        self.assertIn("previous = &*--prev_it;", body)
        self.assertIn("++next_it != context->ass->Events.end()", body)
        self.assertIn("contains(previous, drag_end_ms)", body)
        self.assertIn("contains(previous, drag_start_ms)", body)
        self.assertIn("contains(next, drag_end_ms)", body)
        self.assertIn("contains(next, drag_start_ms)", body)

    def test_caret_split_and_inward_text_transfer(self):
        body = self.method
        self.assertIn("MapDisplayRangeToRaw(", body)
        self.assertIn("SplitAtCaret(", body)
        self.assertIn("line->Text = pieces->before;", body)
        self.assertIn("split_line->Text = pieces->after;", body)
        self.assertIn("contains(adjacent, drag_start_ms) && contains(line, drag_end_ms)", body)
        self.assertIn("if (entering_current)", body)
        self.assertIn("audio_dialog_text::Join(adjacent->Text.get(), pieces->before)", body)
        self.assertIn("audio_dialog_text::Join(pieces->after, adjacent->Text.get())", body)
        self.assertIn("commit_type |= AssFile::COMMIT_DIAG_TEXT;", body)
        self.assertIn("restore_caret(", body)

    def test_previous_and_next_boundaries_are_mirrored(self):
        body = self.method
        for stmt in (
            "adjacent->End = boundary;",
            "line->Start = boundary;",
            "line->End = boundary;",
            "adjacent->Start = boundary;",
            "valid_duration(overall_start, boundary)",
            "valid_duration(boundary, overall_end)",
            "SetSelectionAndActive({ line, adjacent }, line)",
            "TimeRange(overall_start, overall_end)",
        ):
            with self.subTest(stmt=stmt):
                self.assertIn(stmt, body)


if __name__ == "__main__":
    unittest.main()
