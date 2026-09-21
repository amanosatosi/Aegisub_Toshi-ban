// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_tag_edit.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace {

struct TextRange {
	int start;
	int end;
};

struct FadeTagRange {
	size_t start = std::string::npos;
	size_t end = std::string::npos;
	int fade_in = 0;
	int fade_out = 0;
	std::string start_color;
	std::string end_color;
	bool arguments_valid = false;
	bool has_colors = false;
	bool is_fad = false;

	explicit operator bool() const { return start != std::string::npos; }
};

std::string Trim(std::string value) {
	auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
	value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
	return value;
}

bool ParseFadeInteger(std::string token, int& value) {
	token = Trim(std::move(token));
	if (token.empty())
		return false;

	char *end = nullptr;
	errno = 0;
	long parsed = std::strtol(token.c_str(), &end, 10);
	if (errno == ERANGE || end != token.c_str() + token.size() || parsed < std::numeric_limits<int>::min() ||
		parsed > std::numeric_limits<int>::max())
		return false;
	value = static_cast<int>(parsed);
	return true;
}

size_t SkipSpaces(std::string const& text, size_t pos, size_t end) {
	while (pos < end && std::isspace(static_cast<unsigned char>(text[pos])))
		++pos;
	return pos;
}

size_t MatchingParen(std::string const& text, size_t open, size_t end) {
	int depth = 0;
	for (size_t pos = open; pos < end; ++pos) {
		if (text[pos] == '(')
			++depth;
		else if (text[pos] == ')' && --depth == 0)
			return pos;
	}
	return std::string::npos;
}

std::vector<std::string> SplitArguments(std::string const& text, size_t open, size_t close) {
	std::vector<std::string> arguments;
	size_t start = open + 1;
	for (size_t pos = start; pos <= close; ++pos) {
		if (pos == close || text[pos] == ',') {
			arguments.push_back(text.substr(start, pos - start));
			start = pos + 1;
		}
	}
	return arguments;
}

bool ParseFadeArguments(std::string const& text, size_t open, size_t close, FadeTagRange& result) {
	auto arguments = SplitArguments(text, open, close);
	if (arguments.size() != 2 && arguments.size() != 4)
		return false;
	if (!ParseFadeInteger(arguments[0], result.fade_in) ||
		!ParseFadeInteger(arguments[1], result.fade_out))
		return false;

	result.has_colors = arguments.size() == 4;
	if (result.has_colors) {
		result.start_color = Trim(std::move(arguments[2]));
		result.end_color = Trim(std::move(arguments[3]));
	}
	return true;
}

bool ParseLongFadeArguments(std::string const& text, size_t open, size_t close) {
	auto arguments = SplitArguments(text, open, close);
	if (arguments.size() != 7)
		return false;
	int value = 0;
	return std::all_of(arguments.begin(), arguments.end(),
		[&](std::string const& argument) { return ParseFadeInteger(argument, value); });
}

FadeTagRange FindEffectiveFadeTag(std::string const& text) {
	FadeTagRange first_invalid;
	for (size_t block_start = text.find('{'); block_start != std::string::npos;
		block_start = text.find('{', block_start + 1)) {
		size_t const block_end = text.find('}', block_start + 1);
		if (block_end == std::string::npos)
			break;
		if (text.find('\\', block_start + 1) >= block_end)
			continue;

		for (size_t pos = block_start + 1; pos < block_end;) {
			if (text[pos] != '\\') {
				++pos;
				continue;
			}

			size_t const tag_start = pos++;
			while (pos < block_end && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_'))
				++pos;
			std::string const name = text.substr(tag_start, pos - tag_start);
			size_t const value_start = SkipSpaces(text, pos, block_end);
			size_t tag_end = value_start;
			size_t close = std::string::npos;
			if (value_start < block_end && text[value_start] == '(') {
				close = MatchingParen(text, value_start, block_end);
				tag_end = close == std::string::npos ? block_end : close + 1;
			}
			else {
				tag_end = text.find('\\', value_start);
				if (tag_end == std::string::npos || tag_end > block_end)
					tag_end = block_end;
			}

			// Parenthesized tags such as \t are skipped as one unit, so nested
			// \fad tags are not mistaken for line-level tags.
			if (name == "\\fad" || name == "\\fade") {
				FadeTagRange candidate;
				candidate.start = tag_start;
				candidate.end = tag_end;
				candidate.is_fad = name == "\\fad";
				if (close != std::string::npos) {
					candidate.arguments_valid = candidate.is_fad ?
						ParseFadeArguments(text, value_start, close, candidate) :
						ParseLongFadeArguments(text, value_start, close);
				}
				if (candidate.arguments_valid)
					return candidate; // libassmod applies the first valid fade tag.
				if (candidate.is_fad && !first_invalid)
					first_invalid = std::move(candidate);
			}
			pos = std::max(tag_end, tag_start + 1);
		}
		block_start = block_end;
	}
	return first_invalid;
}

bool HasInitialOverrideBlock(std::string const& text) {
	if (text.empty() || text[0] != '{')
		return false;
	size_t const close = text.find('}', 1);
	if (close == std::string::npos)
		return false;
	if (close == 1)
		return true;
	return text.find('\\', 1) < close;
}

agi::ass::AlignmentEditResult InsertAlignmentTag(std::string text, int alignment, int caret) {
	std::vector<TextRange> removals;
	bool in_block = false;
	bool block_is_override = false;
	bool block_has_content = false;
	int block_start = 0;
	size_t block_removal_start = 0;

	for (int pos = 0; pos < static_cast<int>(text.size()); ++pos) {
		char const ch = text[pos];
		if (!in_block) {
			if (ch == '{') {
				in_block = true;
				block_start = pos;
				block_is_override = pos + 1 < static_cast<int>(text.size()) && text[pos + 1] == '\\';
				block_has_content = false;
				block_removal_start = removals.size();
			}
			continue;
		}

		if (block_is_override && ch == '\\' && pos + 3 < static_cast<int>(text.size()) &&
			text[pos + 1] == 'a' && text[pos + 2] == 'n' &&
			text[pos + 3] >= '1' && text[pos + 3] <= '9') {
			removals.push_back({pos, pos + 4});
			pos += 3;
			continue;
		}

		if (ch == '}') {
			if (block_is_override && !block_has_content) {
				removals.resize(block_removal_start);
				removals.push_back({block_start, pos + 1});
			}
			in_block = false;
			continue;
		}

		if (!std::isspace(static_cast<unsigned char>(ch)))
			block_has_content = true;
	}

	for (auto it = removals.rbegin(); it != removals.rend(); ++it) {
		caret = agi::ass::MoveTextPositionAfterEdit(caret, it->start, it->end, 0);
		text.erase(it->start, it->end - it->start);
	}

	std::string const tag = agi::format("\\an%d", alignment);
	int insert_at = 0;
	std::string insertion = "{" + tag + "}";
	if (text.size() >= 2 && text[0] == '{' && text[1] == '\\' && text.find('}') != std::string::npos) {
		insert_at = 1;
		insertion = tag;
	}
	text.insert(insert_at, insertion);
	caret = agi::ass::MoveTextPositionAfterEdit(caret, insert_at, insert_at, static_cast<int>(insertion.size()));
	return {std::move(text), caret};
}

}

namespace agi { namespace ass {

int MoveTextPositionAfterEdit(int position, int edit_start, int edit_end, int replacement_length) {
	if (edit_start == edit_end)
		return position < edit_start ? position : position + replacement_length;
	if (position <= edit_start)
		return position;
	if (position >= edit_end)
		return position + replacement_length - (edit_end - edit_start);
	return edit_start + replacement_length;
}

AlignmentEditResult SetLineAlignment(
	std::string const& text, int alignment, int selection_start, int selection_end) {
	selection_start = std::clamp(selection_start, 0, static_cast<int>(text.size()));
	selection_end = std::clamp(selection_end, 0, static_cast<int>(text.size()));
	if (selection_start > selection_end)
		std::swap(selection_start, selection_end);

	std::string edited = text;
	if (selection_start != selection_end)
		edited.erase(selection_start, selection_end - selection_start);
	return InsertAlignmentTag(std::move(edited), alignment, selection_start);
}

FadeEditResult SetColoredFade(
	std::string const& text, FadeSide side, int milliseconds, std::string const& color) {
	FadeTagRange const existing = FindEffectiveFadeTag(text);
	bool const update_existing_fad = existing && existing.is_fad;
	int fade_in = update_existing_fad && existing.arguments_valid ? std::max(0, existing.fade_in) : 0;
	int fade_out = update_existing_fad && existing.arguments_valid ? std::max(0, existing.fade_out) : 0;
	std::string start_color = update_existing_fad && existing.arguments_valid && existing.has_colors ? existing.start_color : std::string();
	std::string end_color = update_existing_fad && existing.arguments_valid && existing.has_colors ? existing.end_color : std::string();

	if (side == FadeSide::In) {
		fade_in = std::max(0, milliseconds);
		start_color = color;
	}
	else {
		fade_out = std::max(0, milliseconds);
		end_color = color;
	}

	std::string const tag = agi::format("\\fad(%d,%d,%s,%s)",
		fade_in, fade_out, start_color, end_color);
	FadeEditResult result;
	if (update_existing_fad) {
		result.edit_start = static_cast<int>(existing.start);
		result.edit_end = static_cast<int>(existing.end);
		result.text = text.substr(0, existing.start) + tag + text.substr(existing.end);
	}
	else {
		// If a valid long-form \fade wins precedence, insert immediately before
		// it. Otherwise put the new tag in the initial override block as usual.
		result.edit_start = result.edit_end = existing && !existing.is_fad ?
			static_cast<int>(existing.start) : (HasInitialOverrideBlock(text) ? 1 : 0);
		bool const insert_inside_override = result.edit_start != 0 || HasInitialOverrideBlock(text);
		std::string const insertion = insert_inside_override ? tag : "{" + tag + "}";
		result.text = text.substr(0, result.edit_start) + insertion + text.substr(result.edit_end);
		result.replacement_length = static_cast<int>(insertion.size());
		return result;
	}
	result.replacement_length = static_cast<int>(tag.size());
	return result;
}

std::string GetFadeColor(std::string const& text, FadeSide side) {
	FadeTagRange const existing = FindEffectiveFadeTag(text);
	if (!existing.arguments_valid || !existing.is_fad || !existing.has_colors)
		return std::string();
	return side == FadeSide::In ? existing.start_color : existing.end_color;
}

int FadeDurationFromVideoTime(FadeSide side, int video_time, int line_start, int line_end) {
	long long const duration = static_cast<long long>(line_end) - line_start;
	if (duration < 0)
		return -1;
	long long value = side == FadeSide::In ?
		static_cast<long long>(video_time) - line_start :
		static_cast<long long>(line_end) - video_time;
	value = std::max(0LL, std::min(value, duration));
	return static_cast<int>(std::min(value, static_cast<long long>(std::numeric_limits<int>::max())));
}

} }
