// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39.h>
class AssDialogue;
namespace agi { namespace timing39 {
Analysis AnalyzeDialogue(AssDialogue const&);
// On failure, output is untouched; no partial serialization is committed.
bool Serialize(AssDialogue const&, Analysis const&, std::vector<TimingBlock> const&,
	std::vector<TimingAssignment> const&, std::string& output, std::string& error);
std::vector<int> KaraokeBoundaries(AssDialogue const&);
} }
