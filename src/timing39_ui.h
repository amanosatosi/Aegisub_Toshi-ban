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
		text+="\n"+match.reason;
	}
	else text=match.reason;
	if(capture.status==PartitionStatus::AmbiguousSungCrossing)
		text+="\nSung block crosses the line end by more than "+std::to_string(sung_checkpoint_clamp_tolerance_ms)+" ms; review required";
	auto ambiguity=CompactAmbiguity(result.target.analysis,match);
	if(!ambiguity.empty())text+="\n\n"+ambiguity;
	return text;
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
