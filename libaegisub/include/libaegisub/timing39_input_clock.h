// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <cstdint>
#include <limits>

namespace agi { namespace timing39 {
// Both values use the same wrapping, monotonic millisecond clock. On Windows
// this is the tick clock of GetMessageTime/GetTickCount, not wall-clock time.
class InputClockMapper {
	uint32_t anchor_tick = 0;
	int anchor_media = 0;
	bool anchored = false;
public:
	void Reset() { anchored = false; }
	bool IsAnchored() const { return anchored; }
	void Anchor(uint32_t tick, int media_ms) {
		anchor_tick = tick; anchor_media = media_ms; anchored = true;
	}
	// Returns false for stale, future, or uncalibrated event timestamps. Caller
	// may then use the playback clock at handler time as an explicit fallback.
	bool Map(uint32_t event_tick, uint32_t handled_tick, int line_start,
		int line_end, int& media_ms) const {
		if (!anchored) return false;
		int32_t age = int32_t(handled_tick - event_tick);
		int32_t from_anchor = int32_t(event_tick - anchor_tick);
		if (age < -30 || age > 10000 || from_anchor < -30) return false;
		int64_t mapped = int64_t(anchor_media) + from_anchor;
		if (mapped < line_start - 1000LL || mapped > line_end + 1000LL ||
			mapped < 0 || mapped > std::numeric_limits<int>::max()) return false;
		media_ms = int(mapped);
		return true;
	}
};
} }
