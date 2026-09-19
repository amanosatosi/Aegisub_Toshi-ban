// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "timing39_karaoke.h"
#include "ass_dialogue.h"
#include "ass_karaoke.h"
#include <algorithm>

namespace agi { namespace timing39 {
Analysis AnalyzeDialogue(AssDialogue const& line) {
	for(auto const& block:line.ParseTags()) {
		if(block->GetType()==AssBlockType::DRAWING) {
			Analysis a;a.source=line.Text;a.error="Drawing events are not a supported sung reading";return a;
		}
	}
	AssKaraoke kara(&line,false,false);
	auto analysis=Analyze(kara.GetText(false));
	// Keep the untouched input for correction records and stale-preview checks.
	analysis.source=line.Text;
	return analysis;
}

bool Serialize(AssDialogue const& line,Analysis const& a,std::vector<TimingBlock> const& blocks,
	std::vector<TimingAssignment> const& assignments,std::string& output,std::string& error) {
	if(!a.error.empty() || !ValidAssignments(a,blocks,assignments)) {error="No complete legal assignment to serialize";return false;}
	int start=line.Start,end=line.End,previous=start;
	for(auto const& block:blocks) {
		if(block.start<previous || block.end<block.start || block.end>end || (!block.gap&&block.start==block.end)) {
			error="Capture falls outside this dialogue checkpoint or contains unfinished timing";return false;
		}
		previous=block.end;
	}
	AssDialogue surface(line);surface.Text=a.surface;
	AssKaraoke kara(&surface,false,false);
	std::string logical;for(auto const& syl:kara) logical+=syl.text;
	if(logical!=a.logical_text) {error="Reading/source alignment changed; serialization refused";return false;}
	AssKaraoke original(&line,false,false);
	std::string original_logical;for(auto const& syl:original) original_logical+=syl.text;
	std::vector<std::string> types;
	for(auto const& assignment:assignments) {
		std::string type=original.GetTagType();size_t offset=0;
		if(original_logical==logical) for(auto const& syl:original) {
			if(offset>a.morae[assignment.first_mora].logical_begin) break;
			type=syl.tag_type;offset+=syl.text.size();
		}
		types.push_back(type);
	}
	while(kara.size()>1) kara.RemoveSplit(1);
	for(size_t i=1;i<assignments.size();++i) {
		size_t pos=a.morae[assignments[i].first_mora].logical_begin;
		size_t last=i==1?0:a.morae[assignments[i-1].first_mora].logical_begin;
		if(pos<=last || pos>=logical.size()) {error="Unsupported split inside a reading encoding";return false;}
		kara.AddSplitKTiming(i-1,pos-last);
	}
	// Round absolute relative-to-line checkpoints once, then subtract. Rounding
	// each duration independently accumulates drift across a long lyric.
	auto quantize=[&](int ms){return start+((ms-start+5)/10)*10;};
	std::vector<int> boundaries;
	size_t slot=0;previous=start;
	for(size_t i=0;i<assignments.size();++i) {
		auto const& b=blocks[assignments[i].timing_block];
		if(b.start>previous) {
			kara.InsertEmptySyllable(slot);kara.SetSyllableTagType(slot++,types[i],false);boundaries.push_back(quantize(b.start));
		}
		kara.SetSyllableTagType(slot++,types[i],false);
		boundaries.push_back(quantize(b.end));previous=b.end;
	}
	if(previous<end) kara.AppendEmptySyllable(false);
	else if(!boundaries.empty()) boundaries.pop_back();
	kara.SetTimingBoundaries(start,quantize(end),boundaries,false);
	output=kara.GetText();error.clear();return true;
}

std::vector<int> KaraokeBoundaries(AssDialogue const& line) {
	AssKaraoke kara(&line,false,false);std::vector<int> result;
	if(!kara.HasKaraokeTags()) return result;
	for(auto const& syl:kara) if(!syl.text.empty()) {
		result.push_back(syl.start_time);result.push_back(syl.start_time+syl.duration);
	}
	std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
} }
