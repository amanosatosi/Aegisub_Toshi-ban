"""CI contracts for the native tracker/main-video boundary (no GUI startup)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, name):
    start = source.index("DialogMotionTrack::" + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[body:end]


class NativeMotionContracts(unittest.TestCase):
    def setUp(self):
        self.window = (ROOT / "src/dialog_motion_track.cpp").read_text(encoding="utf-8")

    def test_apply_reads_main_reference_after_advanced_dialog(self):
        apply = method(self.window, "ApplyMotion")
        self.assertLess(apply.index("AdvancedMotionApply"), apply.index("int reference = context->videoController->GetFrameN()"))
        self.assertIn("MainTrack(),ClipTrack().frames.empty() ? nullptr : &ClipTrack()", apply)
        self.assertEqual(apply.count("->Commit("), 1)
        self.assertIn("SetSelectionAndActive", apply)
        self.assertLess(apply.index("SetSelectionAndActive"), apply.index("->Commit("))
        self.assertNotIn("current_frame", apply)
        self.assertNotIn("Automation", apply)

    def test_hide_restore_never_owns_a_reference_or_seeks(self):
        create = method(self.window, "CreateControls")
        self.assertIn("StopPlayback(); Hide();", create)
        self.assertIn("Iconize(false); Hide();", create)
        self.assertIn("Bind(wxEVT_SHOW", create)
        self.assertIn("if (event.IsShown()) LoadCurrentFrame();", create)
        self.assertNotIn("videoController->Jump", create)
        self.assertNotIn("ClearData()", create[create.index("minimize->Bind"):create.index("auto bottom")])
        manager = (ROOT / "src/dialog_manager.h").read_text(encoding="utf-8")
        self.assertIn("diag.second->Show();", manager)
        switch = method(self.window, "SwitchTrack")
        self.assertIn("swap(segments,other_channel.segments)", switch)
        self.assertIn("swap(result,other_channel.result)", switch)
        self.assertNotIn("videoController->Jump", switch)

    def test_native_layers_and_tests_are_registered(self):
        for filename in ("motion_track_optimizer.cpp", "motion_track_apply.cpp", "motion_track_commit.cpp", "ass_file_extradata.cpp"):
            self.assertIn(filename, (ROOT / "src/meson.build").read_text())
            self.assertIn(filename, (ROOT / "tests/meson.build").read_text())
        self.assertIn("tests/motion_track_apply.cpp", (ROOT / "tests/meson.build").read_text())
        apply = (ROOT / "src/motion_tracking/motion_track_apply.cpp").read_text()
        self.assertIn("source.ParseTags()", apply)
        self.assertNotIn('"\\\\distort', apply)
        self.assertNotIn('"\\\\perspective', apply)

    def test_apply_modes_only_serialize_one_original_event(self):
        apply = (ROOT / "src/motion_tracking/motion_track_apply.cpp").read_text()
        commit = (ROOT / "src/motion_tracking/motion_track_commit.cpp").read_text()
        install = commit[commit.index("MotionCommitSelection InstallMotionApplications"):commit.index("MotionCommitSelection RevertMotionFamilies")]
        self.assertEqual(apply.count("output.events.push_back("), 1)
        self.assertIn("plan.application.events.size() != 1", install)
        self.assertNotIn("file.Events.insert", install)
        self.assertNotIn("file.Events.erase", install)
        self.assertIn("RemoveMotionLayers(input.Text.get())", apply)
        self.assertIn("Force frame-by-frame transforms", self.window)
        self.assertIn("Select the Mangetsu subtitle renderer", self.window)

    def test_tracking_samples_are_usable_by_apply(self):
        track_step = method(self.window, "TrackOne")
        self.assertIn("ConfirmTrackingSource", track_step)
        self.assertNotIn("source_marker, 1.0, motion_tracking::MotionTrackState::Untracked", track_step)
        track_start = method(self.window, "StartTrackRunHere")
        self.assertIn("MotionTrackState::Tracked", track_start)
        status = method(self.window, "UpdateApplyStatus")
        self.assertIn("IsUsableMotionTrackState", status)

    def test_channel_selector_shares_the_original_action_row(self):
        create = method(self.window, "CreateControls")
        row = create[create.index("auto workflow"):create.index("frame_bar =")]
        controls = ("main_track_button", "reference_label", "subtitle_channel_button", "clip_channel_button")
        positions = [row.index("workflow->Add(" + name + ",") for name in controls]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(row.count("main_sizer->Add(workflow"), 1)
        self.assertEqual(row.count("new wxButton"), 1)
        self.assertEqual(row.count("new wxToggleButton"), 2)
        self.assertIn("workflow->Add(reference_label, 1,", row)
        self.assertNotIn("clip_track_button", self.window)

    def test_selecting_a_channel_only_switches_context(self):
        create = method(self.window, "CreateControls")
        for control, clip in (("subtitle_channel_button", "false"), ("clip_channel_button", "true")):
            binding = next(line for line in create.splitlines() if control + "->Bind(" in line)
            self.assertIn("wxEVT_TOGGLEBUTTON", binding)
            self.assertIn("{ SwitchTrack(" + clip + "); }", binding)
            self.assertNotIn("TrackMotion", binding)
        switch = method(self.window, "SwitchTrack")
        for action in ("TrackMotion(", "TrackOne(", "TrackRange(", "ClearData(", "NewSession(", "CaptureSources(", "RecalculateMotion("):
            self.assertNotIn(action, switch)

    def test_channel_round_trip_swaps_complete_native_state(self):
        switch = method(self.window, "SwitchTrack")
        for field in ("result", "segments", "markers", "handoff_marks", "base_frame", "active_segment", "initial_marker_size"):
            self.assertIn(f"swap({field},other_channel.{field})", switch)
        self.assertIn("swap(settings.mode,other_channel.mode)", switch)
        self.assertLess(switch.index("UpdateSettingsFromControls()"), switch.index("swap(settings.mode"))
        self.assertIn("mode_choice->SetSelection(static_cast<int>(settings.mode))", switch)
        self.assertIn("editing_clip = clip", switch)
        self.assertIn("UpdatePanels()", switch)
        self.assertNotIn(".clear()", switch)
        # No sample reconstruction: state, confidence, size and rotation travel
        # with the complete results/segments rather than being regenerated.
        self.assertNotIn("BuildStitchedMotionResult", switch)

    def test_tracking_action_uses_selected_channel_and_both_directions(self):
        create = method(self.window, "CreateControls")
        self.assertIn("main_track_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { TrackMotion(); })", create)
        action = method(self.window, "TrackMotion")
        self.assertIn("bool clip = editing_clip;", action)
        self.assertNotIn("SwitchTrack(", action)
        forward = action.index("TrackRange(settings.end_frame)")
        backward = action.index("TrackRange(settings.start_frame)")
        self.assertLess(forward, backward)
        self.assertEqual(action.count("JumpToFrame(anchor)"), 2)

    def test_switching_never_changes_either_playhead_or_reference(self):
        for name in ("SwitchTrack", "UpdateChannelControls", "UpdatePanels", "UpdateSettingsFromControls", "UpdateApplyStatus"):
            body = method(self.window, name)
            for action in ("JumpToFrame(", "ShowFrame(", "CalculateSelectedFrameRange(", "LoadCurrentFrame(", "current_frame =", "preview_frame ="):
                self.assertNotIn(action, body)
        status = method(self.window, "UpdateApplyStatus")
        self.assertIn("int reference = context->videoController->GetFrameN()", status)

    def test_missing_clip_disables_only_clip_selection_and_preserves_data(self):
        controls = method(self.window, "UpdateChannelControls")
        self.assertIn("subtitle_channel_button->Enable(!tracking)", controls)
        self.assertIn("clip_channel_button->Enable(clip_available && !tracking)", controls)
        self.assertIn("usable \\\\clip or \\\\iclip", controls)
        self.assertIn("clip && !HasTrackClip()", method(self.window, "SwitchTrack"))
        status = method(self.window, "UpdateApplyStatus")
        self.assertIn("editing_clip && !HasTrackClip() && !tracking", status)
        self.assertIn("SwitchTrack(false)", status)
        self.assertNotIn("ClearData()", status)

    def test_toggle_state_is_exclusive_and_resynchronized(self):
        controls = method(self.window, "UpdateChannelControls")
        self.assertIn("subtitle_channel_button->SetValue(!editing_clip)", controls)
        self.assertIn("clip_channel_button->SetValue(editing_clip)", controls)
        switch = method(self.window, "SwitchTrack")
        guard = switch[:switch.index("StopPlayback()")]
        self.assertIn("clip == editing_clip || tracking", guard)
        self.assertIn("UpdateChannelControls()", guard)
        self.assertIn("UpdateChannelControls()", method(self.window, "UpdateApplyStatus"))
        session = method(self.window, "NewSession")
        self.assertLess(session.index("SwitchTrack(false)"), session.index("ClearData()"))
        self.assertIn("other_channel = TrackChannel{}", session)
        self.assertIn("editing_clip = false", session)
        self.assertIn("LoadCurrentFrame()", session)
        self.assertIn("UpdatePanels()", method(self.window, "LoadCurrentFrame"))
        self.assertIn("UpdateApplyStatus()", method(self.window, "CheckSession"))

    def test_other_controls_use_the_active_state(self):
        bindings = method(self.window, "BindControls")
        for action in ("TrackRange(settings.start_frame)", "TrackOne(current_frame - 1)", "TrackOne(current_frame + 1)", "TrackRange(settings.end_frame)", "MarkHandoffFrame()", "ClearHandoffMark()"):
            self.assertIn(action, bindings)
        for name in ("ClearData", "CopyData", "SaveData", "SetCurrentMarker", "MarkHandoffFrame", "ClearHandoffMark"):
            self.assertNotIn("other_channel", method(self.window, name))
        trail = self.window[self.window.index("DialogMotionTrack::GetTrackTrailMarkers"):self.window.index("DialogMotionTrack::GetHandoffMarks")]
        self.assertIn("result.frames", trail)
        self.assertIn("markers.find", trail)
        self.assertIn("dialog->GetResult()", self.window)

    def test_apply_is_independent_of_editing_channel(self):
        apply = method(self.window, "ApplyMotion")
        self.assertNotIn("editing_clip", apply)
        self.assertIn("options.object_motion = !MainTrack().frames.empty()", apply)
        self.assertIn("MainTrack(),ClipTrack().frames.empty() ? nullptr : &ClipTrack()", apply)
        advanced = self.window[self.window.index("bool AdvancedMotionApply("):self.window.index("void DialogMotionTrack::ApplyMotion(")]
        for label in ("Separate clip track", "Follow main motion track", "Keep clip unchanged"):
            self.assertIn(label, advanced)
        self.assertIn("options.clip_source = static_cast<ClipMotionSource>(clip->GetSelection())", advanced)

    def test_revert_does_not_rebind_a_different_familys_track(self):
        revert = method(self.window, "RevertMotion")
        self.assertIn("!applied_event_ids.count(line->Id)", revert)
        self.assertIn("if (own_family) CaptureSources();", revert)
        self.assertIn("else NewSession();", revert)
        self.assertIn("!context_invalid", revert)


if __name__ == "__main__":
    unittest.main()
