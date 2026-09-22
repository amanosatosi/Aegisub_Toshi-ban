// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include <libaegisub/timing39_session.h>
#include <algorithm>
#include <limits>

namespace agi { namespace timing39 {
SessionPlaybackStart FindSessionPlaybackStart(std::vector<SessionStartEvent> const& events) {
	SessionPlaybackStart chosen;
	for (auto const& event : events) {
		if (!event.comment || event.start < 0 || !event.id) continue;
		auto text = event.plain_text;
		auto first = text.find_first_not_of(" \t\r\n\f\v");
		if (first == std::string::npos) continue;
		text = text.substr(first, text.find_last_not_of(" \t\r\n\f\v") - first + 1);
		for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (text != "39 mode start here") continue;
		if (!chosen.marker || event.start < chosen.time ||
			(event.start == chosen.time && event.id < chosen.marker))
			chosen = {event.start, event.id};
	}
	return chosen;
}
std::string SessionPlaybackStart::Describe() const {
	return "SESSION START: " + std::to_string(time) + " ms (" +
		(marker ? "comment marker: 39 mode start here; event " + std::to_string(marker) : "default media start") + ")\n";
}
bool HasSungBlocks(std::vector<TimingBlock> const& blocks) {
	return std::any_of(blocks.begin(), blocks.end(), [](TimingBlock const& b) { return !b.gap; });
}
PartitionedCapture PartitionCapture(std::vector<TimingBlock> const& raw, int start, int end) {
	PartitionedCapture out;
	for (size_t i = 0; i < raw.size(); ++i) {
		auto const& b = raw[i];
		if (b.end <= start || b.start >= end) continue;
		if (!b.gap && b.start < start) {
			// A physical sung block belongs to exactly one dialogue: the one in
			// which it began. Its tail remains visible in raw diagnostics but is
			// not manufactured into a new local timing unit.
			++out.preceding_sung_tails;
			continue;
		}
		TimingBlock local{std::max(start, b.start), std::min(end, b.end), b.gap};
		if (local.end <= local.start) continue;
		if (b.gap && (local.start != b.start || local.end != b.end))
			out.status = std::max(out.status, PartitionStatus::HarmlessClamp);
		else if (!b.gap && b.end > end) {
			auto crossing = b.end - end;
			out.status = std::max(out.status, crossing <= sung_checkpoint_clamp_tolerance_ms
				? PartitionStatus::HarmlessClamp : PartitionStatus::AmbiguousSungCrossing);
		}
		out.blocks.push_back(local);
		out.raw_indices.push_back(i);
	}
	// An untouched lane is absent, not a line full of artificial gaps.
	if (!HasSungBlocks(out.blocks)) { out.blocks.clear(); out.raw_indices.clear(); }
	return out;
}
std::vector<TimingBlock> ShiftCapture(std::vector<TimingBlock> const& raw, int correction_ms) {
	std::vector<TimingBlock> adjusted;
	adjusted.reserve(raw.size());
	for (auto block : raw) {
		auto shift = [correction_ms](int value) {
			return int(std::max<int64_t>(0, std::min<int64_t>(std::numeric_limits<int>::max(),
				int64_t(value) + correction_ms)));
		};
		block.start = shift(block.start);
		block.end = shift(block.end);
		adjusted.push_back(block);
	}
	return adjusted;
}
std::vector<SessionTarget> DiscoverTargets(std::vector<SessionTarget> const& candidates,
	bool explicit_scope, std::string const& active_style, int start, int end) {
	std::vector<SessionTarget> out;
	for (auto target : candidates) {
		if (explicit_scope) {
			if (!target.selected) continue;
			target.discovery_reason = "explicit selected scope";
		}
		else {
			if (target.start >= end || target.end <= start || !target.lyric_evidence) continue;
			// Other styles need actual karaoke or ruby evidence; Japanese signs
			// alone do not qualify as backing lyrics.
			bool ruby = std::any_of(target.analysis.spans.begin(), target.analysis.spans.end(),
				[](SourceSpan const& s) { return s.explicit_reading; });
			if (target.style != active_style && target.existing_boundaries.empty() && !ruby) continue;
			target.discovery_reason = target.style == active_style ? "active lyric style and Japanese/ruby evidence" : "overlapping ruby/karaoke evidence; lane requires review";
		}
		if (target.end > target.start) out.push_back(std::move(target));
	}
	std::stable_sort(out.begin(), out.end(), [](SessionTarget const& a, SessionTarget const& b) { return a.start < b.start; });
	return out;
}
Confidence SessionResult::GetConfidence() const {
	if (lane < 0 || lane > 1) return Confidence::Yellow;
	auto status = lanes[lane].match.confidence;
	if (status == Confidence::Red) return status;
	if (lanes[lane].capture.status == PartitionStatus::AmbiguousSungCrossing) return Confidence::Yellow;
	if (!reviewed && (association_ambiguous || overlap)) return Confidence::Yellow;
	if (resolution == ResolutionSource::UserSelected && !manual_invalidated &&
		ValidAssignments(target.analysis, lanes[lane].capture.blocks, lanes[lane].editor.Get()))
		return Confidence::Green;
	return status;
}
void Timing39Session::Prepare(std::vector<SessionTarget> targets, bool selected, std::string style, int begin, int finish) {
	Discard(); candidates = std::move(targets); explicit_scope = selected; active_style = std::move(style);
	start = std::max(0, begin); end = std::max(start, finish); captured_end = start;
	countdown = 3; state = SessionState::Countdown;
}
bool Timing39Session::TickCountdown() {
	if (state != SessionState::Countdown) return false;
	if (--countdown) return false;
	state = SessionState::Ready; return true;
}
bool Timing39Session::Start(int ms) {
	if (state != SessionState::Ready) return false;
	start = ms; captured_end = ms;
	auto& raw = IsRetake() ? retake_capture : capture;
	for (int lane = 0; lane < 2; ++lane) {
		if (!IsRetake() || lane == retake_lane)
			raw.lanes[lane].Begin(IsRetake() ? results[retake_result].target.start : ms, end);
		else raw.lanes[lane].Clear();
	}
	retake_held = {{false,false}}; armed_owner = -1; retake_boundary_started = false;
	state = SessionState::Capturing; return true;
}
bool Timing39Session::Key(int key, bool down, int ms) {
	if (state != SessionState::Capturing) return false;
	int lane = key == 'F' || key == 'J' ? 0 : key == 'D' || key == 'K' ? 1 : -1;
	if (lane < 0 || (IsRetake() && lane != retake_lane)) return false;
	if (IsRetake()) {
		int boundary = results[retake_result].target.start;
		if (ms < boundary && !retake_boundary_started) {
			int index = lane == 0 ? (key == 'F' ? 0 : 1) : (key == 'D' ? 0 : 1);
			retake_held[index] = down;
			if (down) armed_owner = index;
			else if (armed_owner == index) armed_owner = retake_held[1-index] ? 1-index : -1;
			return true;
		}
		Advance(ms);
	}
	auto& raw = IsRetake() ? retake_capture : capture;
	return down ? raw.lanes[lane].KeyDown(key, ms) : raw.lanes[lane].KeyUp(key, ms);
}
void Timing39Session::Advance(int ms) {
	if (!IsRetake() || state != SessionState::Capturing || retake_boundary_started ||
		ms < results[retake_result].target.start) return;
	retake_boundary_started = true;
	if (armed_owner >= 0 && retake_held[armed_owner]) {
		int key = retake_lane == 0 ? (armed_owner ? 'J' : 'F') : (armed_owner ? 'K' : 'D');
		retake_capture.lanes[retake_lane].KeyDown(key, results[retake_result].target.start);
	}
	retake_held = {{false,false}}; armed_owner = -1;
}
char Timing39Session::ArmedOwner() const {
	if (!IsRetake() || state != SessionState::Capturing || retake_boundary_started || armed_owner < 0) return 0;
	return char(retake_lane == 0 ? (armed_owner ? 'J' : 'F') : (armed_owner ? 'K' : 'D'));
}
void Timing39Session::Rematch(size_t row, int lane) {
	if (row >= results.size() || lane < 0 || lane > 1) return;
	auto& r = results[row]; auto& l = r.lanes[lane];
	l.match = Match(r.target.analysis, l.capture.blocks);
	l.editor.Reset(l.match.paths.empty() ? std::vector<TimingAssignment>{} : l.match.paths[0].assignments);
	r.reviewed = false; r.committed = false; r.manual_invalidated = false;
	r.resolution = ResolutionSource::Automatic;
}
bool Timing39Session::ChooseAssignment(size_t row, int lane, size_t path) {
	if (state != SessionState::Results || row >= results.size() || lane < 0 || lane > 1) return false;
	auto& r = results[row]; auto& l = r.lanes[lane];
	if (r.committed) return false;
	if (path >= l.match.paths.size() || l.match.confidence == Confidence::Red) return false;
	auto const& chosen = l.match.paths[path].assignments;
	if (!ValidAssignments(r.target.analysis, l.capture.blocks, chosen)) return false;
	if (chosen != l.editor.Get()) l.editor.Choose(r.target.analysis, chosen);
	r.lane = lane; r.reviewed = true; r.manual_invalidated = false;
	r.resolution = ResolutionSource::UserSelected;
	return true;
}
bool Timing39Session::SetTimingCorrection(int correction_ms) {
	if (state != SessionState::Results || correction_ms < -2000 || correction_ms > 2000 ||
		std::any_of(results.begin(), results.end(), [](SessionResult const& r){return r.committed;})) return false;
	if (correction_ms == timing_correction_ms) return true;
	timing_correction_ms = correction_ms;
	std::array<std::vector<TimingBlock>, 2> adjusted{{
		ShiftCapture(Raw(0), correction_ms), ShiftCapture(Raw(1), correction_ms)}};
	for (auto& r : results) {
		bool previous_manual = r.resolution == ResolutionSource::UserSelected;
		r.manual_invalidated = false;
		for (int lane = 0; lane < 2; ++lane) {
			auto& l = r.lanes[lane];
			auto const* source = &adjusted[lane];
			std::vector<TimingBlock> adjusted_retake;
			for (auto take = retakes.rbegin(); take != retakes.rend(); ++take)
				if (take->target == r.target.id && take->lane == lane) {
					adjusted_retake = ShiftCapture(take->raw, correction_ms);
					source = &adjusted_retake;
					break;
				}
			l.capture = PartitionCapture(*source, r.target.start, r.target.end);
			l.match = Match(r.target.analysis, l.capture.blocks);
			if (previous_manual && r.lane == lane &&
				ValidAssignments(r.target.analysis, l.capture.blocks, l.editor.Get())) continue;
			if (previous_manual && r.lane == lane) {
				r.manual_invalidated = true; r.resolution = ResolutionSource::Automatic; r.reviewed = false;
			}
			l.editor.Reset(l.match.paths.empty() ? std::vector<TimingAssignment>{} : l.match.paths[0].assignments);
		}
	}
	return true;
}
void Timing39Session::Resolve() {
	auto targets = DiscoverTargets(candidates, explicit_scope, active_style, start, captured_end);
	std::vector<StyleEvidence> evidence;
	auto boundaries = [](PartitionedCapture const& p) {
		std::vector<int> out;
		for (auto const& b : p.blocks) if (!b.gap) { out.push_back(b.start); out.push_back(b.end); }
		std::sort(out.begin(), out.end()); out.erase(std::unique(out.begin(), out.end()), out.end()); return out;
	};
	for (auto const& t : targets) evidence.push_back({t.style, t.existing_boundaries,
		boundaries(PartitionCapture(Raw(0), t.start, t.end)), boundaries(PartitionCapture(Raw(1), t.start, t.end))});
	auto styles = InferStyles(evidence);
	bool secondary = HasSungBlocks(Raw(1));
	for (auto const& target : targets) {
		SessionResult r; r.target = target;
		for (int i = 0; i < 2; ++i) r.lanes[i].capture = PartitionCapture(Raw(i), target.start, target.end);
		if (secondary && !styles.ambiguous) {
			r.lane = target.style == styles.primary ? 0 : target.style == styles.secondary ? 1 : -1;
			r.association_ambiguous = r.lane < 0;
		}
		else if (secondary || (!explicit_scope && target.style != active_style)) {
			r.lane = target.style == active_style ? 0 : -1;
			r.association_ambiguous = true;
		}
		results.push_back(std::move(r));
		Rematch(results.size()-1, 0); Rematch(results.size()-1, 1);
	}
	for (size_t i = 0; i < results.size(); ++i) for (size_t j = i+1; j < results.size(); ++j) {
		auto& a = results[i]; auto& b = results[j];
		if (a.lane == b.lane && a.target.start < b.target.end && b.target.start < a.target.end)
			a.overlap = b.overlap = true;
	}
}
bool Timing39Session::Stop(int ms) {
	if (state == SessionState::Countdown || state == SessionState::Ready) {
		countdown = 0; state = SessionState::Results; retake_result = unknown; return true;
	}
	if (state != SessionState::Capturing) return false;
	captured_end = std::max(start, std::min(ms, end));
	auto& raw = IsRetake() ? retake_capture : capture;
	raw.Finish(captured_end);
	if (IsRetake()) {
		auto& result = results[retake_result];
		auto const& blocks = raw.lanes[retake_lane].Blocks();
		retakes.push_back({result.target.id, retake_lane, blocks});
		result.lanes[retake_lane].capture = PartitionCapture(
			ShiftCapture(blocks, timing_correction_ms), result.target.start, result.target.end);
		Rematch(retake_result, retake_lane);
		result.resolution = ResolutionSource::Retake;
		retake_result = unknown;
	}
	else Resolve();
	state = SessionState::Results; return true;
}
bool Timing39Session::Retake(size_t row, int lane, int preroll) {
	if (state != SessionState::Results || row >= results.size() || lane < 0 || lane > 1 || results[row].committed) return false;
	retake_result = row; retake_lane = lane;
	start = std::max(0, results[row].target.start - std::max(0, preroll)); end = results[row].target.end;
	countdown = 3; state = SessionState::Countdown; return true;
}
bool Timing39Session::CancelRetake() {
	if (!IsRetake()) return false;
	retake_result = unknown; retake_capture = {}; retake_held = {{false,false}};
	armed_owner = -1; retake_boundary_started = false; countdown = 0;
	state = SessionState::Results; return true;
}
void Timing39Session::Discard() {
	capture = {}; retake_capture = {}; candidates.clear(); results.clear(); retakes.clear();
	timing_correction_ms = 0; retake_held = {{false,false}}; armed_owner = -1; retake_boundary_started = false;
	retake_result = unknown; countdown = 0; state = SessionState::Idle;
}
std::vector<TimingBlock> Timing39Session::Preview(int lane, int ms) const {
	return (IsRetake() ? retake_capture : capture).lanes[lane].Preview(ms);
}
std::vector<TimingBlock> Timing39Session::Preview(int lane, int ms, int visible_start, int visible_end) const {
	return (IsRetake() ? retake_capture : capture).lanes[lane].Preview(ms,visible_start,visible_end);
}
} }
