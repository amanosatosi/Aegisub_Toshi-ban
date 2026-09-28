// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39.h>
#include <cstdint>

namespace agi { namespace timing39 {
enum class SessionState { Idle, Countdown, Ready, Capturing, Results };
struct SessionStartEvent {
	uint64_t id;
	bool comment;
	int start;
	std::string plain_text; // ASS adapter removes override/drawing blocks
};
struct SessionPlaybackStart {
	int time = 0;
	uint64_t marker = 0; // zero means default media start; event IDs are one-based
	std::string Describe() const;
};
SessionPlaybackStart FindSessionPlaybackStart(std::vector<SessionStartEvent> const&);
struct SessionTarget {
	uint64_t id = 0;
	int start = 0, end = 0;
	std::string style;
	Analysis analysis;
	std::vector<int> existing_boundaries;
	bool selected = false, lyric_evidence = false;
	std::string discovery_reason;
};
// A sung block is eligible for a line only when strictly more than half its
// original (globally corrected, but not locally clipped) duration is inside.
// Gaps are geometric context and can be clipped without affecting confidence.
enum class PartitionStatus { Clean, HarmlessClamp, NeedsReview, Invalid };
struct PartitionedCapture {
	std::vector<TimingBlock> blocks;
	std::vector<size_t> raw_indices;
	std::vector<size_t> disputed_raw_indices;
	PartitionStatus status = PartitionStatus::Clean;
	size_t preceding_sung_tails = 0;
};
int64_t SungOverlapDuration(TimingBlock const&, int start, int end);
bool SungMajorityOwned(TimingBlock const&, int start, int end);
PartitionedCapture PartitionCapture(std::vector<TimingBlock> const&, int start, int end);
std::vector<TimingBlock> ShiftCapture(std::vector<TimingBlock> const&, int correction_ms);
bool HasSungBlocks(std::vector<TimingBlock> const&);
std::vector<SessionTarget> DiscoverTargets(std::vector<SessionTarget> const&, bool explicit_scope,
	std::string const& active_style, int start, int end);

struct SessionLaneResult {
	PartitionedCapture capture;
	PartitionedCapture automatic_capture;
	std::vector<TimingBlock> timing_override;
	bool timing_override_active = false;
	MatchResult match;
	AssignmentEditor editor;
	std::vector<TimingAssignment> manual_assignments;
	bool manual_active = false;
	std::vector<TimingAssignment> const& Assignments() const { return manual_active ? manual_assignments : editor.Get(); }
};
// Manual mappings preserve block order but may repeat a mora interval for
// successive sung attacks. Unlike automatic candidates, they are explicit
// performer decisions and are not constrained by the candidate graph.
bool ValidManualAssignments(Analysis const&, std::vector<TimingBlock> const&,
	std::vector<TimingAssignment> const&);
enum class ResolutionSource { Automatic, UserSelected, UserManualRepair, UserManualTiming, Retake };
struct SessionResult {
	SessionTarget target;
	std::array<SessionLaneResult, 2> lanes;
	int lane = 0;
	bool association_ambiguous = false, overlap = false, reviewed = false, committed = false;
	bool manual_invalidated = false;
	ResolutionSource resolution = ResolutionSource::Automatic;
	Confidence GetConfidence() const;
};
struct SessionRetake {
	uint64_t target;
	int lane;
	std::vector<TimingBlock> raw;
};

// Owns the complete performance, including inter-line silence. Dialogue-local
// results are derived only after stop; capture does not consult a lyric cursor.
class Timing39Session {
	SessionState state = SessionState::Idle;
	TimingCaptureSession capture;
	TimingCaptureSession retake_capture;
	std::vector<SessionTarget> candidates;
	std::vector<SessionResult> results;
	std::string active_style;
	bool explicit_scope = false;
	int countdown = 0, start = 0, end = 0, captured_end = 0;
	size_t retake_result = unknown;
	int retake_lane = 0;
	int timing_correction_ms = 0;
	std::array<bool, 2> retake_held{{false,false}};
	int armed_owner = -1;
	bool retake_boundary_started = false;
	void Resolve();
public:
	std::vector<SessionRetake> retakes;
	void Prepare(std::vector<SessionTarget>, bool explicit_selection, std::string style,
		int playback_start, int playback_end);
	bool TickCountdown(); // returns true exactly when playback should start
	bool Start(int media_position);
	bool Key(int key, bool down, int media_position);
	bool Stop(int media_position); // playing -> results; idempotent
	void Discard();
	bool Retake(size_t result, int lane, int preroll);
	bool CancelRetake();
	void Advance(int media_position);
	char ArmedOwner() const;
	int TimingCorrection() const { return timing_correction_ms; } // positive moves taps later
	bool SetTimingCorrection(int correction_ms);
	bool ChooseAssignment(size_t row, int lane, size_t path);
	bool SetManualAssignment(size_t row, int lane, std::vector<TimingAssignment> mapping);
	bool ResetManualAssignment(size_t row, int lane);
	bool SetManualTiming(size_t row, int lane, std::vector<TimingBlock> sung_blocks);
	bool ResetManualTiming(size_t row, int lane);
	void Rematch(size_t result, int lane);
	SessionState State() const { return state; }
	int Countdown() const { return countdown; }
	int StartTime() const { return start; }
	int EndTime() const { return end; }
	int CapturedEnd() const { return captured_end; }
	bool IsRetake() const { return retake_result != unknown; }
	std::vector<SessionTarget> const& Targets() const { return candidates; }
	std::vector<SessionResult>& Results() { return results; }
	std::vector<SessionResult> const& Results() const { return results; }
	std::vector<TimingBlock> const& Raw(int lane) const { return capture.lanes[lane].Blocks(); }
	std::vector<TimingBlock> const& RawForResult(size_t row, int lane) const;
	std::vector<TimingBlock> Preview(int lane, int media_position) const;
	std::vector<TimingBlock> Preview(int lane, int media_position, int visible_start, int visible_end) const;
	template<typename Visitor> void VisitPreview(int lane, int media_position, int visible_start,
		int visible_end, Visitor visitor) const {
		(IsRetake() ? retake_capture : capture).lanes[lane].VisitPreview(
			media_position, visible_start, visible_end, visitor);
	}
};
} }
