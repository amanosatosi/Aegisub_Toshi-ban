// Copyright (c) 2026, Aegisub Project
// Distributed under the BSD license; see mkv_wrap.cpp.
#pragma once

#include <libaegisub/ass/time.h>

#include <optional>
#include <string>
#include <utility>

namespace matroska {
// The range must refer to one direct or decompressed text subtitle packet.
// Interior NUL bytes are deliberately preserved.
inline const char *TrimTextPacketEnd(const char *begin, const char *end) {
	while (end > begin && end[-1] == '\0')
		--end;
	return end;
}

// Convert a packet to the ordered ASS dialogue consumed by AssParser.
std::optional<std::pair<int, std::string>> ParseTextPacket(
	const char *begin, const char *end, bool srt,
	agi::Time start, agi::Time finish, int srt_order);
}
