// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include "timing39_session_setup.h"
#include "timing39_karaoke.h"
#include "ass_dialogue.h"
#include <algorithm>
#include <set>

namespace agi { namespace timing39 {
SessionSetup BuildSessionSetup(std::vector<AssDialogue*> const& events,
	std::vector<AssDialogue*> const& selected, AssDialogue const* active, int media_end) {
	SessionSetup setup;
	std::set<AssDialogue const*> lyrics;
	for (auto line : selected) if (!line->Comment) lyrics.insert(line);
	setup.explicit_scope = lyrics.size() > 1;
	setup.playback_end = setup.explicit_scope ? 0 : media_end;
	if (active && !active->Comment) setup.active_style = active->Style;
	else for (auto line : events) if (!line->Comment && (lyrics.empty() || lyrics.count(line))) {
		setup.active_style = line->Style; break;
	}
	std::vector<SessionStartEvent> markers;
	for (size_t i = 0; i < events.size(); ++i) {
		auto line = events[i];
		if (line->Comment) markers.push_back({i+1, true, int(line->Start), line->GetStrippedText()});
	}
	setup.playback_start = FindSessionPlaybackStart(markers);
	for (size_t i = 0; i < events.size(); ++i) {
		auto line = events[i];
		if (line->Comment) continue; // metadata never reaches language analysis
		if (setup.explicit_scope && !lyrics.count(line)) continue;
		if (!setup.explicit_scope && int(line->End) <= setup.playback_start.time) continue;
		SessionTarget target;
		target.id = i+1; target.start = line->Start; target.end = line->End; target.style = line->Style;
		target.analysis = AnalyzeDialogue(*line);
		target.existing_boundaries = KaraokeBoundaries(*line);
		target.selected = lyrics.count(line) != 0;
		target.lyric_evidence = !target.analysis.morae.empty() &&
			std::any_of(target.analysis.reading.characters.begin(), target.analysis.reading.characters.end(),
				[](ReadingCharacter const& ch) { return ch.kana >= U'ぁ' && ch.kana <= U'ー'; });
		if (setup.explicit_scope) setup.playback_end = std::max(setup.playback_end, target.end);
		setup.targets.push_back(std::move(target));
	}
	return setup;
}
} }
