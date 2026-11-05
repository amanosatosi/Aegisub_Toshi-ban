// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

class AssDialogue;
class AssFile;

namespace mangetsu {
bool IsMetadataComment(AssDialogue const& line);
// Changing the first ordinary event can open/close the initial metadata block.
bool MayAffectMetadata(AssFile const& subs, AssDialogue const& target);
}
