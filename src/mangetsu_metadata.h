// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

class AssDialogue;
class AssFile;

namespace mangetsu {
// Return the canonical renderer Effect, or nullptr for an ordinary event.
const char *MetadataEffect(AssDialogue const& line);
bool IsStyleMetadata(AssDialogue const& line);
bool IsMetadataComment(AssDialogue const& line);
bool HasInheritanceMarker(AssDialogue const& line);
// Changing the first ordinary event can open/close the initial metadata block.
bool MayAffectMetadata(AssFile const& subs, AssDialogue const& target);
}
