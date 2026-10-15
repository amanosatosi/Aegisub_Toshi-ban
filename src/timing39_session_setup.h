// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <libaegisub/timing39_session.h>
class AssDialogue;
namespace agi { namespace timing39 {
struct SessionSetup {
	SessionPlaybackStart playback_start;
	int playback_end = 0;
	bool explicit_scope = false;
	std::string active_style;
	std::vector<SessionTarget> targets;
};
// Event position + 1 is the stable snapshot ID used for targets and diagnostics.
SessionSetup BuildSessionSetup(std::vector<AssDialogue*> const& events,
	std::vector<AssDialogue*> const& selected, AssDialogue const* active, int media_end);
} }
