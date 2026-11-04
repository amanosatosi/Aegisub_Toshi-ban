// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "mangetsu_metadata.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <boost/algorithm/string/trim.hpp>

namespace mangetsu {
bool IsMetadataComment(AssDialogue const& line) {
	if (!line.Comment) return false;
	// Style defaults do not require an actor. Keep the existing actor metadata
	// matching policy, but share one contiguous block with style declarations.
	if (line.Effect.get() == "mangetsu-style") return true;
	return !line.Actor.get().empty()
		&& boost::trim_copy(line.Effect.get()) == "mangetsu-colorcoding";
}

bool MayAffectMetadata(AssFile const& subs, AssDialogue const& target) {
	for (auto const& line : subs.Events) {
		if (line.Row == target.Row) return line.Comment;
		if (!IsMetadataComment(line)) return false;
	}
	return false;
}
}
