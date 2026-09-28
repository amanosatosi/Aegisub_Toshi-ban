// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once

#include <libaegisub/timing39_session.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace agi { namespace timing39 { namespace ui {
struct Rgb { unsigned char red, green, blue; };
enum class StatusRole { Green, Amber, Red };
struct StatusVisual {
	StatusRole role;
	Rgb accent;
	Rgb tint;
};

struct LyricSegment { std::string base, ruby; };
struct LyricDisplay { std::string plain; std::vector<LyricSegment> segments; };

// Prepared once for each result row. SourceSpan is the reading pipeline's
// structured output, so explicit <display|reading> always wins over a lexicon.
inline LyricDisplay PrepareLyricDisplay(Analysis const& analysis) {
	LyricDisplay display;
	for (auto const& span : analysis.spans) {
		std::string base = span.display;
		if (base == "\\N" || base == "\\n" || base == "\\h") base = " ";
		bool non_ascii = std::any_of(base.begin(), base.end(),
			[](unsigned char ch){return ch >= 0x80;});
		std::string ruby = (span.explicit_reading || (non_ascii && base != span.original_reading))
			? span.original_reading : std::string{};
		display.plain += base;
		display.segments.push_back({std::move(base), std::move(ruby)});
	}
	return display;
}

inline std::string CompactPathChoice(Analysis const& analysis, MatchResult const& match, size_t path) {
	if (path >= match.paths.size()) return {};
	std::string label;
	for (auto boundary : match.ambiguous_mora_boundaries) {
		if (!boundary || boundary >= analysis.morae.size()) continue;
		bool cut = false;
		for (auto const& assignment : match.paths[path].assignments)
			if (assignment.first_mora + assignment.mora_count == boundary) { cut = true; break; }
		if (!label.empty()) label += "  /  ";
		label += analysis.morae[boundary-1].text;
		label += cut ? " | " : "";
		label += analysis.morae[boundary].text;
	}
	return label;
}

constexpr StatusVisual StatusStyle(Confidence status) {
	return status == Confidence::Green ? StatusVisual{StatusRole::Green,{46,125,50},{232,245,233}} :
		status == Confidence::Yellow ? StatusVisual{StatusRole::Amber,{183,121,31},{255,248,225}} :
		StatusVisual{StatusRole::Red,{198,40,40},{253,236,236}};
}

inline size_t SungTapCount(PartitionedCapture const& capture) {
	return std::count_if(capture.blocks.begin(),capture.blocks.end(),[](TimingBlock const& block){return !block.gap;});
}

inline size_t LegalMergeCandidateCount(Analysis const& analysis) {
	size_t count=0;
	for(auto const& edges:analysis.graph)for(auto const& edge:edges)if(edge.count>1)++count;
	return count;
}

inline std::string CompactAmbiguity(Analysis const& analysis,MatchResult const& match) {
	if(match.ambiguous_mora_boundaries.empty())return {};
	std::string text="Ambiguous grouping\n";
	for(auto boundary:match.ambiguous_mora_boundaries) {
		if(!boundary || boundary>=analysis.morae.size())continue;
		text+="\n"+analysis.morae[boundary-1].text+" | "+analysis.morae[boundary].text+"\n"+
			analysis.morae[boundary-1].text+analysis.morae[boundary].text+"\n";
	}
	return text;
}

inline std::string CompactResultReason(SessionResult const& result,int lane) {
	if(lane<0 || lane>1)return "Lane assignment requires review";
	auto const& capture=result.lanes[lane].capture;
	auto const& match=result.lanes[lane].match;
	auto morae=result.target.analysis.morae.size(),taps=SungTapCount(capture);
	std::string text;
	if(match.confidence==Confidence::Red) {
		text=std::to_string(morae)+" morae / "+std::to_string(taps)+" taps";
		if(taps<morae) {
			text+="\n"+std::to_string(morae-taps)+" merge"+(morae-taps==1?"":"s")+" required";
			text+="\n"+std::to_string(LegalMergeCandidateCount(result.target.analysis))+" legal merge candidates";
		}
		else if(taps>morae)
			text+="\n"+std::to_string(taps-morae)+" extra attack"+(taps-morae==1?"":"s")+
				" must be assigned to an adjacent mora or retaken";
		text+="\n"+match.reason;
	}
	else text=match.reason;
	if(capture.status==PartitionStatus::NeedsReview)
		text+="\nTwo target lines claim the same sung block equally; review its owner";
	if(capture.status==PartitionStatus::Invalid)
		text+="\nInvalid zero/negative-duration sung block in this line";
	auto ambiguity=CompactAmbiguity(result.target.analysis,match);
	if(!ambiguity.empty())text+="\n\n"+ambiguity;
	return text;
}

enum class ReviewOwnership {
	Gap, Owned, ExcludedPreviousTail, ExcludedOtherLine,
	HarmlessStartClamp, HarmlessEndClamp, HarmlessBothClamp, CrossingNeedsReview
};
struct ReviewBlock {
	TimingBlock raw, adjusted;
	ReviewOwnership ownership;
};
struct LocalReviewModel {
	int begin = 0, end = 0, line_start = 0, line_end = 0;
	std::vector<ReviewBlock> raw;
	std::vector<TimingBlock> local;
	std::vector<TimingAssignment> assignments;
};
struct ReviewBitmapKey {
	int begin = 0, end = 0, width = 0, height = 0;
	uint64_t generation = 0;
	bool operator==(ReviewBitmapKey const& rhs) const {
		return begin == rhs.begin && end == rhs.end && width == rhs.width &&
			height == rhs.height && generation == rhs.generation;
	}
};
inline std::pair<int,int> ReviewPlaybackRange(LocalReviewModel const& view, bool context) {
	return context ? std::make_pair(view.begin, view.end) :
		std::make_pair(view.line_start, view.line_end);
}
inline LocalReviewModel BuildLocalReviewModel(SessionResult const& result, int lane,
	std::vector<TimingBlock> const& raw, int correction_ms, int context_ms = 400) {
	LocalReviewModel view;
	view.line_start = result.target.start; view.line_end = result.target.end;
	view.begin = std::max(0, view.line_start - context_ms);
	view.end = view.line_end + context_ms;
	if (lane < 0 || lane > 1) return view;
	// Captures are chronological. Selection/correction rebuilds only this local
	// view; OnPaint never walks or copies the full session.
	auto first = std::lower_bound(raw.begin(), raw.end(), view.begin - correction_ms,
		[](TimingBlock const& b, int t){ return b.end <= t; });
	for (auto at = first; at != raw.end() && at->start < view.end - correction_ms; ++at) {
		auto shift = [correction_ms](int value) {
			return int(std::max<int64_t>(0, std::min<int64_t>(std::numeric_limits<int>::max(),
				int64_t(value) + correction_ms)));
		};
		TimingBlock shifted{shift(at->start), shift(at->end), at->gap};
		auto index = size_t(at - raw.begin());
		auto const& lane_result = result.lanes[lane];
		auto const& capture = lane_result.timing_override_active ?
			lane_result.automatic_capture : lane_result.capture;
		ReviewOwnership ownership = ReviewOwnership::Owned;
		if (at->gap) ownership = ReviewOwnership::Gap;
		else if (!std::binary_search(capture.raw_indices.begin(), capture.raw_indices.end(), index))
			ownership = shifted.start < view.line_start ? ReviewOwnership::ExcludedPreviousTail :
				ReviewOwnership::ExcludedOtherLine;
		else if (std::binary_search(capture.disputed_raw_indices.begin(),
			capture.disputed_raw_indices.end(), index))
			ownership = ReviewOwnership::CrossingNeedsReview;
		else if (shifted.start < view.line_start && shifted.end > view.line_end)
			ownership = ReviewOwnership::HarmlessBothClamp;
		else if (shifted.start < view.line_start)
			ownership = ReviewOwnership::HarmlessStartClamp;
		else if (shifted.end > view.line_end)
			ownership = ReviewOwnership::HarmlessEndClamp;
		view.raw.push_back({*at, shifted, ownership});
	}
	view.local = result.lanes[lane].capture.blocks;
	view.assignments = result.lanes[lane].Assignments();
	return view;
}

// Start-sorted interval index with a monotonic prefix maximum. A visible-range
// query can skip every definitely-finished event, while still finding a long
// event which began well before the viewport. Painting never parses subtitles
// or walks the complete event list.
class TimelineIntervalIndex {
	struct Entry { int start,end,prefix_max_end; };
	std::vector<Entry> entries;
public:
	void Reset(std::vector<std::pair<int,int>> spans) {
		entries.clear();entries.reserve(spans.size());
		for(auto span:spans)if(span.second>span.first)entries.push_back({span.first,span.second,span.second});
		std::stable_sort(entries.begin(),entries.end(),[](Entry const& a,Entry const& b){return a.start<b.start;});
		int prefix=0;for(auto& entry:entries){prefix=std::max(prefix,entry.end);entry.prefix_max_end=prefix;}
	}
	template<typename Visitor> void Visit(int begin,int end,Visitor visitor) const {
		if(end<=begin)return;
		auto at=std::lower_bound(entries.begin(),entries.end(),begin,[](Entry const& entry,int value){return entry.prefix_max_end<=value;});
		for(;at!=entries.end()&&at->start<end;++at)if(at->end>begin)visitor(at->start,at->end);
	}
	size_t Size() const {return entries.size();}
};
} } }
