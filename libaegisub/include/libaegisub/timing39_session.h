// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39.h>
#include <cstdint>

namespace agi { namespace timing39 {
enum class SessionState { Idle, Countdown, Ready, Capturing, Results };
struct SessionTarget {
	uint64_t id = 0;
	int start = 0, end = 0;
	std::string style;
	Analysis analysis;
	std::vector<int> existing_boundaries;
	bool selected = false, lyric_evidence = false;
	std::string discovery_reason;
};
struct PartitionedCapture {
	std::vector<TimingBlock> blocks;
	std::vector<size_t> raw_indices;
	bool clipped = false;
};
PartitionedCapture PartitionCapture(std::vector<TimingBlock> const&, int start, int end);
bool HasSungBlocks(std::vector<TimingBlock> const&);
std::vector<SessionTarget> DiscoverTargets(std::vector<SessionTarget> const&, bool explicit_scope,
	std::string const& active_style, int start, int end);

struct SessionLaneResult {
	PartitionedCapture capture;
	MatchResult match;
	AssignmentEditor editor;
};
struct SessionResult {
	SessionTarget target;
	std::array<SessionLaneResult, 2> lanes;
	int lane = 0;
	bool association_ambiguous = false, overlap = false, reviewed = false, committed = false;
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
	std::vector<TimingBlock> Preview(int lane, int media_position) const;
};
} }
