// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "timing39_karaoke.h"
#include <libaegisub/timing39_session.h>
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

bool BuildCommitPlan(AssDialogue const& line,Analysis const& a,std::vector<TimingBlock> const& blocks,
	std::vector<TimingAssignment> const& assignments,CommitPlan& plan,std::string& error,bool manual) {
	if(!a.error.empty() || !(manual ? ValidManualAssignments(a,blocks,assignments) :
		ValidAssignments(a,blocks,assignments))) {error="No complete legal assignment to serialize";return false;}
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
	CommitPlan next;next.surface=a.surface;next.logical_text=logical;next.start=start;next.end=end;
	auto append=[&](CommitAtomKind kind,size_t from,size_t to,int begin,int finish,
		size_t timing_block,size_t assignment,std::string const& tag) {
		CommitAtom atom;atom.kind=kind;atom.logical_begin=from;atom.logical_end=to;
		atom.start=begin;atom.end=finish;atom.timing_block=timing_block;
		atom.assignment=assignment;atom.tag_type=tag;
		for(auto const& ch:a.reading.characters)if(ch.logical_begin<to && ch.logical_end>from) {
			auto const& span=a.spans[ch.span];
			atom.source_begin=std::min(atom.source_begin,span.begin);
			atom.source_end=atom.source_end==unknown?span.end:std::max(atom.source_end,span.end);
		}
		next.atoms.push_back(std::move(atom));
	};
	size_t cursor=0;
	previous=start;
	for(size_t i=0;i<assignments.size();++i) {
		auto const& mapping=assignments[i];
		auto const& block=blocks[mapping.timing_block];
		bool repeated=i && mapping.first_mora==assignments[i-1].first_mora &&
			mapping.mora_count==assignments[i-1].mora_count;
		auto first=a.morae[mapping.first_mora].logical_begin;
		if(!repeated && (first<cursor || a.morae[mapping.first_mora+mapping.mora_count-1].logical_end>logical.size())) {
			error="Unsupported split inside a reading encoding";return false;
		}
		// The initial captured gap precedes opening punctuation. Later source
		// anchors stay immediately after the preceding sung text, before a gap.
		if(!i && block.start>previous)append(CommitAtomKind::CapturedGap,cursor,cursor,previous,block.start,
			unknown,unknown,types[i]);
		if(!repeated && first>cursor)append(CommitAtomKind::ZeroText,cursor,first,
			i?previous:block.start,i?previous:block.start,unknown,unknown,types[i]);
		if(!repeated)cursor=first;
		if(i && block.start>previous)append(CommitAtomKind::CapturedGap,first,first,previous,block.start,
			unknown,unknown,types[i]);
		if(repeated)append(CommitAtomKind::Sung,cursor,cursor,block.start,block.end,
			mapping.timing_block,i,types[i]);
		else {
			size_t run_begin=first,run_morae=0,consumed=0;
			for(size_t m=mapping.first_mora;m<mapping.first_mora+mapping.mora_count;++m) {
				auto const& mora=a.morae[m];
				bool manual_cut=std::binary_search(a.manual_latin_cuts.begin(),
					a.manual_latin_cuts.end(),mora.logical_begin);
				if(mora.logical_begin>cursor || (m>mapping.first_mora&&manual_cut)) {
					if(run_morae) {
						auto split=block.start+int(int64_t(block.end-block.start)*consumed/mapping.mora_count);
						auto finish=block.start+int(int64_t(block.end-block.start)*(consumed+run_morae)/mapping.mora_count);
						append(CommitAtomKind::Sung,run_begin,cursor,split,finish,mapping.timing_block,i,types[i]);
						consumed+=run_morae;run_morae=0;
					}
					if(mora.logical_begin>cursor) {
						auto when=block.start+int(int64_t(block.end-block.start)*consumed/mapping.mora_count);
						append(CommitAtomKind::ZeroText,cursor,mora.logical_begin,when,when,unknown,unknown,types[i]);
					}
					run_begin=mora.logical_begin;
				}
				cursor=mora.logical_end;++run_morae;
			}
			if(run_morae) {
				auto split=block.start+int(int64_t(block.end-block.start)*consumed/mapping.mora_count);
				append(CommitAtomKind::Sung,run_begin,cursor,split,block.end,mapping.timing_block,i,types[i]);
			}
		}
		previous=block.end;
	}
	if(cursor<logical.size())append(CommitAtomKind::ZeroText,cursor,logical.size(),previous,previous,
		unknown,unknown,types.back());
	if(previous<end)append(CommitAtomKind::CapturedGap,logical.size(),logical.size(),previous,end,
		unknown,unknown,types.back());
	if(next.atoms.empty()) {error="No commit atoms";return false;}
	plan=std::move(next);error.clear();return true;
}

bool SerializeCommitPlan(AssDialogue const& line,CommitPlan& plan,std::string& output,std::string& error) {
	if(plan.start!=int(line.Start) || plan.end!=int(line.End) || plan.atoms.empty()) {
		error="Commit plan no longer matches the dialogue checkpoint";return false;
	}
	AssDialogue surface(line);surface.Text=plan.surface;
	AssKaraoke kara(&surface,false,false);
	std::string logical;for(auto const& syl:kara)logical+=syl.text;
	if(logical!=plan.logical_text) {error="Commit plan source alignment changed";return false;}
	while(kara.size()>1)kara.RemoveSplit(1);
	size_t cursor=0,split_index=0,previous_split=0;
	for(auto const& atom:plan.atoms)if(atom.logical_end>atom.logical_begin) {
		if(atom.logical_begin!=cursor || atom.logical_end>logical.size()) {
			error="Commit plan text atoms are not source-ordered";return false;
		}
		cursor=atom.logical_end;
		if(cursor<logical.size()) {
			kara.AddSplitKTiming(split_index++,cursor-previous_split);
			previous_split=cursor;
		}
	}
	if(cursor!=logical.size()) {error="Commit plan omitted visible source text";return false;}
	// Recut with cumulative byte offsets; empty captured gaps and repeated
	// attacks are inserted after the source text has been partitioned.
	if(kara.size()!=size_t(std::count_if(plan.atoms.begin(),plan.atoms.end(),
		[](CommitAtom const& atom){return atom.logical_end>atom.logical_begin;}))) {
		error="Commit plan split failed at a source character boundary";return false;
	}
	size_t slot=0;
	for(auto const& atom:plan.atoms) {
		if(atom.logical_begin==atom.logical_end)kara.InsertEmptySyllable(slot);
		kara.SetSyllableTagType(slot++,atom.tag_type,false);
	}
	if(kara.size()!=plan.atoms.size()) {error="Commit plan slot count changed";return false;}
	auto quantize=[&](int ms){return plan.start+((ms-plan.start+5)/10)*10;};
	std::vector<int> boundaries;boundaries.reserve(plan.atoms.size()-1);
	for(size_t i=0;i+1<plan.atoms.size();++i)boundaries.push_back(quantize(plan.atoms[i].end));
	kara.SetTimingBoundaries(plan.start,quantize(plan.end),boundaries,false);
	auto fragments=kara.GetTextFragments();
	if(fragments.size()!=plan.atoms.size()) {error="Commit plan fragment count changed";return false;}
	std::string next;
	for(size_t i=0;i<fragments.size();++i) {
		plan.atoms[i].fragment=fragments[i];next+=fragments[i];
	}
	if(next!=kara.GetText()) {error="Commit plan preview differs from karaoke serialization";return false;}
	output=std::move(next);error.clear();return true;
}

bool Serialize(AssDialogue const& line,Analysis const& a,std::vector<TimingBlock> const& blocks,
	std::vector<TimingAssignment> const& assignments,std::string& output,std::string& error,bool manual) {
	CommitPlan plan;
	if(!BuildCommitPlan(line,a,blocks,assignments,plan,error,manual))return false;
	return SerializeCommitPlan(line,plan,output,error);
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
