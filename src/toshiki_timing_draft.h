// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39.h>
#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

// Shared boundary rules for Toshiki's explicit timing markers and the
// Results-local draft. Neither operation writes an ASS line or raw capture.
namespace toshiki_timing {
inline int Round(int ms) { return (ms + 5) / 10 * 10; }
inline int Clamp(int ms, int minimum, int maximum) {
	return std::max(minimum, std::min(Round(ms), maximum));
}

// Do not append a pending timing boundary by clamping a click in an earlier,
// already-timed syllable to the most recent boundary.
inline bool CanAppendBoundary(int ms, int minimum, int end) {
	return ms >= minimum && ms <= end;
}

// A new text cut inside an already-timed syllable creates a temporary boundary
// at the old end. Left-clicks in the new *left* part retime that cut; clicks
// in the right part (or in untouched syllables) are for listening instead.
inline int FindReCutBoundary(std::vector<int> const& boundaries,
	std::vector<size_t> const& recuts, int line_start, int ms) {
	for (auto index : recuts) {
		if (index >= boundaries.size()) continue;
		int begin = index ? boundaries[index - 1] : line_start;
		if (ms >= begin && ms < boundaries[index])
			return static_cast<int>(index);
	}
	return -1;
}

inline void InsertReCut(std::vector<size_t>& recuts, size_t index) {
	for (auto& existing : recuts)
		if (existing >= index) ++existing;
	recuts.push_back(index);
}

inline void RemoveBoundaryReCut(std::vector<size_t>& recuts, size_t index) {
	recuts.erase(std::remove(recuts.begin(), recuts.end(), index), recuts.end());
	for (auto& existing : recuts)
		if (existing > index) --existing;
}

class Draft {
	int line_start = 0, line_end = 0;
	std::vector<std::vector<agi::timing39::TimingBlock>> history;
	size_t cursor = 0;
	void Save(std::vector<agi::timing39::TimingBlock> next) {
		history.resize(cursor + 1);history.push_back(std::move(next));++cursor;
	}
public:
	void Begin(int start, int end, std::vector<agi::timing39::TimingBlock> const& capture) {
		line_start = start;line_end = end;
		std::vector<agi::timing39::TimingBlock> sung;
		for(auto const& block:capture)if(!block.gap)sung.push_back(block);
		if(sung.empty() && end > start)sung.push_back({start,end,false});
		history.clear();history.push_back(std::move(sung));cursor = 0;
	}
	std::vector<agi::timing39::TimingBlock> const& Blocks() const {return history[cursor];}
	bool Changed() const {return cursor > 0;}
	bool Undo() {if(!cursor)return false;--cursor;return true;}
	bool Redo() {if(cursor+1>=history.size())return false;++cursor;return true;}
	bool Move(size_t index, bool start, int ms) {
		auto next=Blocks();
		if(!MoveInPlace(next,index,start,ms))return false;
		Save(std::move(next));return true;
	}
	bool PreviewMove(size_t index, bool start, int ms) {
		return MoveInPlace(history[cursor],index,start,ms);
	}
	void FinishPreview(std::vector<agi::timing39::TimingBlock> const& original) {
		if(history[cursor]==original)return;
		auto changed=history[cursor];history[cursor]=original;Save(std::move(changed));
	}
	bool MoveInPlace(std::vector<agi::timing39::TimingBlock>& next,
		size_t index,bool start,int ms) const {
		if(index>=next.size()||line_end<=line_start)return false;
		auto& block=next[index];
		int minimum=start?(index?next[index-1].end:line_start):block.start+10;
		int maximum=start?block.end-10:(index+1<next.size()?next[index+1].start:line_end);
		if(minimum>maximum)return false;
		int position=Clamp(ms,minimum,maximum);
		if(position==(start?block.start:block.end))return false;
		if(start)block.start=position;else block.end=position;
		return true;
	}
	bool Split(int ms) {
		int position=Round(ms);auto next=Blocks();
		for(size_t i=0;i<next.size();++i)if(position>=next[i].start+10 &&
			position<=next[i].end-10) {
			int old_end=next[i].end;next[i].end=position;
			next.insert(next.begin()+i+1,{position,old_end,false});
			Save(std::move(next));return true;
		}
		return false;
	}
	bool Join(size_t left) {
		if(left+1>=Blocks().size())return false;
		auto next=Blocks();next[left].end=next[left+1].end;
		next.erase(next.begin()+left+1);Save(std::move(next));return true;
	}
};
}
