"""Headless behavior coverage for the production channel switch, run in CI.

Compile the real SwitchTrack/UpdateChannelControls bodies and TrackChannel
definition with native motion types and small UI doubles. No wx app, video
provider or tracking engine is started. Event wiring has separate contracts.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_motion_apply_controls import ROOT, method


class MotionChannelBehavior(unittest.TestCase):
    def test_production_switch_preserves_both_tracks_and_exclusive_selection(self):
        window = (ROOT / "src/dialog_motion_track.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/dialog_motion_track.h").read_text(encoding="utf-8")
        start = header.index("struct TrackChannel {")
        channel = header[start:header.index("\n\t};", start) + len("\n\t};")]
        switch = method(window, "SwitchTrack")
        controls = method(window, "UpdateChannelControls")
        harness = r'''
#include "motion_tracking/motion_track_segments.h"
#include <cassert>
#include <map>
#include <set>
#include <utility>
using namespace motion_tracking;
#define _(text) text

struct Toggle {
    bool value = false, enabled = true;
    void SetValue(bool v) { value = v; }
    void Enable(bool v) { enabled = v; }
    void SetToolTip(char const*) {}
};
struct Choice {
    int selection = 3;
    void SetSelection(int v) { selection = v; }
};
struct DialogMotionTrack {
    CHANNEL_DEFINITION
    MotionTrackSettings settings;
    MotionTrackResult result;
    std::vector<MotionTrackSegment> segments;
    std::map<int, MotionTrackMarker> markers;
    std::set<int> handoff_marks;
    TrackChannel other_channel;
    bool editing_clip = false, tracking = false, clip_available = true;
    int base_frame = -1, active_segment = -1;
    double initial_marker_size = 80;
    int current_frame = 2088, preview_frame = 2088, main_reference = 2088;
    Toggle subtitle, clip;
    Choice mode;
    Toggle* subtitle_channel_button = &subtitle;
    Toggle* clip_channel_button = &clip;
    Choice* mode_choice = &mode;
    bool HasTrackClip() const { return clip_available; }
    void StopPlayback() {}
    void UpdateSettingsFromControls() { settings.mode = static_cast<MotionTrackMode>(mode.selection); }
    void UpdatePanels() { UpdateChannelControls(); }
    void UpdateChannelControls() CONTROLS_BODY
    void SwitchTrack(bool clip) SWITCH_BODY
};

void CheckSelection(DialogMotionTrack const& d, bool clip) {
    assert(d.editing_clip == clip);
    assert(d.subtitle.value == !clip && d.clip.value == clip);
    assert(d.current_frame == 2088 && d.preview_frame == 2088 && d.main_reference == 2088);
}

int main() {
    DialogMotionTrack d;
    d.UpdateChannelControls();
    CheckSelection(d, false);
    MotionTrackFrame sample;
    sample.frame = 2088; sample.x = 17; sample.y = 29;
    sample.scale_x = 1.4; sample.scale_y = 1.4; sample.rotation_deg = 23;
    sample.confidence = 0.65; sample.state = MotionTrackState::WeakTracked;
    d.result.frames.push_back(sample);
    d.result.source_width = 1920; d.result.source_height = 1080; d.result.fps = 25;
    MotionTrackMarker marker;
    marker.cx = 17; marker.cy = 29; marker.size = 112; marker.rotation_deg = 23;
    d.markers[2088] = marker;
    MotionTrackSegment segment;
    segment.anchor_frame = 2088; segment.target_frame = 2095; segment.end_frame_manual = true;
    segment.tracker_box_at_start = marker;
    segment.tracked_center_by_frame.push_back({2088, marker, 0.65, MotionTrackState::WeakTracked});
    segment.accumulated_state_at_anchor.scale_x = 1.4;
    segment.accumulated_state_at_anchor.rotation_deg = 23;
    d.segments.push_back(segment);
    d.handoff_marks.insert(2095);
    d.base_frame = 2088; d.active_segment = 0; d.initial_marker_size = 112;
    d.mode.selection = static_cast<int>(MotionTrackMode::PositionRotation);

    d.SwitchTrack(true);
    CheckSelection(d, true);
    assert(!d.tracking && d.result.frames.empty() && d.markers.empty() && d.segments.empty());
    sample.x = 91; sample.confidence = 0.42; sample.state = MotionTrackState::Predicted;
    d.result.frames.push_back(sample);
    marker.cx = 91; marker.size = 64;
    d.markers[2088] = marker;
    d.handoff_marks.insert(2100);
    d.initial_marker_size = 64;
    d.mode.selection = static_cast<int>(MotionTrackMode::PositionSize);

    d.SwitchTrack(false);
    CheckSelection(d, false);
    assert(!d.tracking && d.result.frames.size() == 1);
    auto restored = d.result.frames.front();
    assert(restored.x == 17 && restored.y == 29 && restored.confidence == 0.65);
    assert(restored.state == MotionTrackState::WeakTracked);
    assert(restored.scale_x == 1.4 && restored.scale_y == 1.4 && restored.rotation_deg == 23);
    assert(d.result.source_width == 1920 && d.result.source_height == 1080 && d.result.fps == 25);
    assert(d.markers.at(2088).cx == 17 && d.markers.at(2088).size == 112);
    assert(d.markers.at(2088).rotation_deg == 23);
    assert(d.segments.size() == 1 && d.segments[0].target_frame == 2095);
    assert(d.segments[0].tracker_box_at_start.size == 112 && d.segments[0].end_frame_manual);
    assert(d.segments[0].tracked_center_by_frame[0].confidence == 0.65);
    assert(d.segments[0].tracked_center_by_frame[0].state == MotionTrackState::WeakTracked);
    assert(d.segments[0].accumulated_state_at_anchor.rotation_deg == 23);
    assert(d.handoff_marks == std::set<int>{2095});
    assert(d.base_frame == 2088 && d.active_segment == 0 && d.initial_marker_size == 112);
    assert(d.settings.mode == MotionTrackMode::PositionRotation && d.mode.selection == 1);

    // Clicking the selected toggle must immediately restore its selected state.
    d.subtitle.value = false;
    d.SwitchTrack(false);
    CheckSelection(d, false);
    // An unavailable clip disables only that option and never touches stored data.
    d.clip_available = false;
    d.SwitchTrack(true);
    CheckSelection(d, false);
    assert(d.subtitle.enabled && !d.clip.enabled);
    assert(d.other_channel.result.frames.front().x == 91);
    d.clip_available = true;
    d.SwitchTrack(true);
    CheckSelection(d, true);
    assert(d.result.frames.front().state == MotionTrackState::Predicted);
    assert(d.result.frames.front().confidence == 0.42 && d.markers.at(2088).size == 64);
    assert(d.handoff_marks == std::set<int>{2100});
    assert(d.settings.mode == MotionTrackMode::PositionSize && d.mode.selection == 2);
    // A busy tracker rejects a channel change and restores both toggle values.
    d.tracking = true;
    d.subtitle.value = true;
    d.SwitchTrack(false);
    CheckSelection(d, true);
    assert(!d.subtitle.enabled && !d.clip.enabled);
}
'''
        harness = (harness.replace("CHANNEL_DEFINITION", channel)
                   .replace("CONTROLS_BODY", controls).replace("SWITCH_BODY", switch))
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "motion_channels.cpp"
            binary = Path(temp) / "motion_channels"
            source.write_text(harness, encoding="utf-8")
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "src"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
