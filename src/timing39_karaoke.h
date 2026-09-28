// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39.h>
class AssDialogue;
namespace agi { namespace timing39 {
Analysis AnalyzeDialogue(AssDialogue const&);
enum class CommitAtomKind { Sung, CapturedGap, ZeroText };
struct CommitAtom {
	CommitAtomKind kind = CommitAtomKind::Sung;
	size_t logical_begin = 0, logical_end = 0;
	size_t source_begin = unknown, source_end = unknown;
	size_t timing_block = unknown, assignment = unknown;
	int start = 0, end = 0;
	std::string tag_type;
	std::string fragment; // exact piece of the final serialized line
};
struct CommitPlan {
	std::string surface, logical_text;
	int start = 0, end = 0;
	std::vector<CommitAtom> atoms;
};
bool BuildCommitPlan(AssDialogue const&, Analysis const&, std::vector<TimingBlock> const&,
	std::vector<TimingAssignment> const&, CommitPlan&, std::string& error, bool manual = false);
bool SerializeCommitPlan(AssDialogue const&, CommitPlan&, std::string& output, std::string& error);
// On failure, output is untouched; no partial serialization is committed.
bool Serialize(AssDialogue const&, Analysis const&, std::vector<TimingBlock> const&,
	std::vector<TimingAssignment> const&, std::string& output, std::string& error,
	bool manual = false);
std::vector<int> KaraokeBoundaries(AssDialogue const&);
} }
