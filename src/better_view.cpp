// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "better_view.h"

#include <algorithm>

namespace {
char const motion_marker[] = "[toshiban motion track helper]";
char const motion_placeholder[] = "{motion tracking}";

bool is_utf8_continuation(char ch) {
	return (static_cast<unsigned char>(ch) & 0xC0) == 0x80;
}

void append_identity(agi::BetterViewConversion& result, std::string const& raw, size_t begin, size_t end) {
	for (size_t pos = begin; pos < end; ++pos) {
		result.display_to_raw.push_back(static_cast<int>(pos));
		result.display_text.push_back(raw[pos]);
	}
}
}

namespace agi {

BetterViewConversion BuildBetterViewConversion(
	std::string const& raw_text,
	bool enabled,
	std::vector<size_t> const& expanded_motion_blocks)
{
	BetterViewConversion result;
	result.raw_text = raw_text;
	result.display_text.reserve(raw_text.size());
	result.display_to_raw.reserve(raw_text.size() + 1);

	if (!enabled) {
		append_identity(result, raw_text, 0, raw_text.size());
		result.display_to_raw.push_back(static_cast<int>(raw_text.size()));
		return result;
	}

	size_t raw_pos = 0;
	while (raw_pos < raw_text.size()) {
		if (raw_text[raw_pos] == '{' &&
			raw_text.compare(raw_pos + 1, sizeof(motion_marker) - 1, motion_marker) == 0)
		{
			size_t close = raw_text.find('}', raw_pos + 1 + sizeof(motion_marker) - 1);
			if (close != std::string::npos) {
				size_t raw_end = close + 1;
				size_t display_begin = result.display_text.size();
				bool expanded = std::find(expanded_motion_blocks.begin(), expanded_motion_blocks.end(), raw_pos) != expanded_motion_blocks.end();

				if (expanded)
					append_identity(result, raw_text, raw_pos, raw_end);
				else {
					for (char ch : std::string(motion_placeholder)) {
						result.display_to_raw.push_back(static_cast<int>(raw_pos));
						result.display_text.push_back(ch);
					}
				}

				result.motion_blocks.push_back({
					raw_pos,
					raw_end,
					display_begin,
					result.display_text.size(),
					!expanded
				});
				raw_pos = raw_end;
				continue;
			}
		}

		if (raw_pos + 1 < raw_text.size() && raw_text[raw_pos] == '\\' && raw_text[raw_pos + 1] == 'N') {
			result.display_to_raw.push_back(static_cast<int>(raw_pos));
			result.display_text.push_back('\n');
			raw_pos += 2;
			continue;
		}

		append_identity(result, raw_text, raw_pos, raw_pos + 1);
		++raw_pos;
	}

	result.display_to_raw.push_back(static_cast<int>(raw_text.size()));
	return result;
}

bool BetterViewConversion::MapDisplayRangeToRaw(int display_start, int display_end, int& raw_start, int& raw_end) const {
	if (display_to_raw.empty()) {
		raw_start = display_start;
		raw_end = display_end;
		return false;
	}

	if (display_start > display_end)
		std::swap(display_start, display_end);
	int max_display = static_cast<int>(display_to_raw.size()) - 1;
	display_start = std::clamp(display_start, 0, max_display);
	display_end = std::clamp(display_end, 0, max_display);

	if (display_start == display_end) {
		for (auto const& block : motion_blocks) {
			if (!block.collapsed || display_start <= static_cast<int>(block.display_begin) || display_start >= static_cast<int>(block.display_end))
				continue;
			size_t midpoint = block.display_begin + (block.display_end - block.display_begin) / 2;
			raw_start = raw_end = display_start < static_cast<int>(midpoint) ?
				static_cast<int>(block.raw_begin) : static_cast<int>(block.raw_end);
			return true;
		}
	}

	raw_start = display_to_raw[display_start];
	raw_end = display_to_raw[display_end];
	for (auto const& block : motion_blocks) {
		if (!block.collapsed || display_start >= static_cast<int>(block.display_end) || display_end <= static_cast<int>(block.display_begin))
			continue;
		raw_start = std::min(raw_start, static_cast<int>(block.raw_begin));
		raw_end = std::max(raw_end, static_cast<int>(block.raw_end));
	}
	return true;
}

int BetterViewConversion::MapRawToDisplay(int raw_offset) const {
	raw_offset = std::clamp(raw_offset, 0, static_cast<int>(raw_text.size()));
	for (auto const& block : motion_blocks) {
		if (!block.collapsed || raw_offset < static_cast<int>(block.raw_begin) || raw_offset > static_cast<int>(block.raw_end))
			continue;
		if (raw_offset == static_cast<int>(block.raw_begin))
			return static_cast<int>(block.display_begin);
		if (raw_offset == static_cast<int>(block.raw_end))
			return static_cast<int>(block.display_end);
		size_t midpoint = block.raw_begin + (block.raw_end - block.raw_begin) / 2;
		return raw_offset < static_cast<int>(midpoint) ?
			static_cast<int>(block.display_begin) : static_cast<int>(block.display_end);
	}

	int display_length = static_cast<int>(display_to_raw.size()) - 1;
	for (int display = 0; display < display_length; ++display) {
		if (raw_offset < display_to_raw[display + 1])
			return display;
	}
	return std::max(0, display_length);
}

BetterViewMotionBlock const* BetterViewConversion::MotionBlockAtDisplayPosition(int display_pos) const {
	for (auto const& block : motion_blocks) {
		if (display_pos >= static_cast<int>(block.display_begin) && display_pos < static_cast<int>(block.display_end))
			return &block;
	}
	return nullptr;
}

std::string BetterViewConversion::MotionBlockText(BetterViewMotionBlock const& block) const {
	if (block.raw_begin > block.raw_end || block.raw_end > raw_text.size())
		return "";
	return raw_text.substr(block.raw_begin, block.raw_end - block.raw_begin);
}

std::string BetterViewDisplayToAss(std::string const& display_text) {
	std::string result;
	result.reserve(display_text.size());
	for (size_t pos = 0; pos < display_text.size(); ++pos) {
		if (display_text[pos] == '\r') {
			if (pos + 1 < display_text.size() && display_text[pos + 1] == '\n')
				++pos;
			result += "\\N";
		}
		else if (display_text[pos] == '\n')
			result += "\\N";
		else
			result += display_text[pos];
	}
	return result;
}

BetterViewEdit ApplyBetterViewEdit(BetterViewConversion const& previous, std::string const& edited_display_text) {
	BetterViewEdit result;
	if (edited_display_text == previous.display_text) {
		result.raw_text = previous.raw_text;
		return result;
	}

	size_t prefix = 0;
	size_t common_length = std::min(previous.display_text.size(), edited_display_text.size());
	while (prefix < common_length && previous.display_text[prefix] == edited_display_text[prefix])
		++prefix;
	while (prefix > 0 && prefix < previous.display_text.size() && is_utf8_continuation(previous.display_text[prefix]))
		--prefix;

	size_t suffix = 0;
	while (suffix < previous.display_text.size() - prefix &&
		suffix < edited_display_text.size() - prefix &&
		previous.display_text[previous.display_text.size() - suffix - 1] == edited_display_text[edited_display_text.size() - suffix - 1])
	{
		++suffix;
	}
	while (suffix > 0) {
		size_t old_end = previous.display_text.size() - suffix;
		size_t new_end = edited_display_text.size() - suffix;
		if ((old_end >= previous.display_text.size() || !is_utf8_continuation(previous.display_text[old_end])) &&
			(new_end >= edited_display_text.size() || !is_utf8_continuation(edited_display_text[new_end])))
			break;
		--suffix;
	}

	int raw_begin = 0;
	int raw_end = 0;
	previous.MapDisplayRangeToRaw(
		static_cast<int>(prefix),
		static_cast<int>(previous.display_text.size() - suffix),
		raw_begin,
		raw_end);

	std::string inserted = BetterViewDisplayToAss(edited_display_text.substr(
		prefix,
		edited_display_text.size() - prefix - suffix));
	result.replaced_raw_begin = static_cast<size_t>(raw_begin);
	result.replaced_raw_end = static_cast<size_t>(raw_end);
	result.inserted_raw_size = inserted.size();
	result.raw_text = previous.raw_text.substr(0, result.replaced_raw_begin) +
		inserted + previous.raw_text.substr(result.replaced_raw_end);
	return result;
}

} // namespace agi
