// Copyright (c) 2026, Aegisub Project
// Distributed under the BSD license; see mkv_wrap.cpp.
#include "mkv_subtitle_packet.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/iterator_range.hpp>

namespace matroska {
std::optional<std::pair<int, std::string>> ParseTextPacket(
	const char *begin, const char *end, bool srt,
	agi::Time start, agi::Time finish, int srt_order) {
	if (begin == end) return std::nullopt;
	using str_range = boost::iterator_range<const char *>;

	if (!srt) {
		auto first = std::find(begin, end, ',');
		if (first == end) return std::nullopt;
		auto second = std::find(first + 1, end, ',');
		if (second == end) return std::nullopt;

		return std::make_pair(
			boost::lexical_cast<int>(str_range(begin, first)),
			agi::format("Dialogue: %d,%s,%s,%s"
				, boost::lexical_cast<int>(str_range(first + 1, second))
				, start.GetAssFormatted()
				, finish.GetAssFormatted()
				, str_range(second + 1, end)));
	}

	auto line = agi::format("Dialogue: 0,%s,%s,Default,,0,0,0,,%s"
		, start.GetAssFormatted()
		, finish.GetAssFormatted()
		, str_range(begin, end));
	boost::replace_all(line, "\r\n", "\\N");
	boost::replace_all(line, "\r", "\\N");
	boost::replace_all(line, "\n", "\\N");
	return std::make_pair(srt_order, std::move(line));
}
}
