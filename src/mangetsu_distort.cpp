// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "mangetsu_distort.h"

#include "ass_dialogue.h"
#include "perspective_geometry.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace {
struct SourceTag {
	size_t begin, end;
	AssOverrideTag tag;
};

// Use ASS parsing for semantics, retaining byte ranges for lossless edits.
// Parenthesized transforms are opaque: their nested tags are not edit targets.
std::vector<SourceTag> SourceTags(std::string const& source, size_t begin, size_t end) {
	std::vector<SourceTag> tags;
	size_t start = begin;
	int depth = 0;
	for (size_t i = begin; i <= end; ++i) {
		if (i == end || (source[i] == '\\' && depth == 0 && i != start)) {
			size_t tag_end = i;
			if (source.compare(start, 9, "\\distort(") == 0) {
				auto close = source.find(')', start + 9);
				if (close < i) tag_end = close + 1;
			}
			tags.push_back({start, tag_end, AssOverrideTag(source.substr(start, tag_end - start))});
			if (source.compare(start, 9, "\\distort(") == 0 && tag_end > start + 9 && source[tag_end - 1] == ')') {
				auto parameters = source.substr(start + 9, tag_end - start - 10);
				size_t count = 1 + std::count(parameters.begin(), parameters.end(), ',');
				bool empty_seventh = count == 7 && parameters.find_last_not_of(" \t\r\n") == parameters.rfind(',');
				if (empty_seventh)
					tags.back().tag.SetText("\\distort(" + parameters.substr(0, parameters.rfind(',')) + ')');
				if (count != 6 && count != 8 && !empty_seventh) tags.back().tag.Clear();
			}
			start = i;
		}
		if (i == end) break;
		if (source[i] == '(') ++depth;
		else if (source[i] == ')' && depth > 0) --depth;
	}
	return tags;
}

size_t BlockEnd(AssDialogueBlock& block, std::string const& source, size_t begin) {
	if (block.GetType() == AssBlockType::OVERRIDE)
		return source.find('}', begin) + 1;
	return begin + block.GetText().size();
}

bool ValidDistort(AssOverrideTag const& tag) {
	return tag.Name == "\\distort" && tag.IsValid() && tag.Params.size() >= 6 &&
		!(tag.Params.size() >= 8 && !tag.Params[6].omitted && tag.Params[7].omitted);
}

bool ChangesRenderRun(AssOverrideTag const& tag) {
	if (!tag.IsValid() || tag.Name == "\\distort") return false;
	// These set event placement/layout/clipping, not an individual glyph's
	// effective style. They must not cut a shared multiline distortion block.
	for (auto name : {"\\pos", "\\move", "\\org", "\\an", "\\a", "\\q", "\\clip", "\\iclip"})
		if (tag.Name == name) return false;
	return true;
}

bool HasRenderableText(std::string const& text) {
	for (size_t position = 0; position < text.size();) {
		if (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n') {
			++position;
			continue;
		}
		if (position + 1 < text.size() && text[position] == '\\' && (text[position + 1] == 'N' || text[position + 1] == 'n' || text[position + 1] == 'h')) {
			position += 2;
			continue;
		}
		if (position + 1 < text.size() && static_cast<unsigned char>(text[position]) == 0xC2 &&
			static_cast<unsigned char>(text[position + 1]) == 0xA0)
		{
			position += 2;
			continue;
		}
		return true;
	}
	return false;
}

bool IsRenderable(AssDialogueBlock const& block) {
	if (block.GetType() == AssBlockType::PLAIN)
		return HasRenderableText(static_cast<AssDialogueBlockPlain const&>(block).text);
	if (block.GetType() == AssBlockType::DRAWING)
		return !static_cast<AssDialogueBlockDrawing const&>(block).text.empty();
	return false;
}

void ApplyTag(MangetsuDistortState& state, AssOverrideTag const& tag) {
	if (tag.Name == "\\r") {
		state = MangetsuDistortState();
		return;
	}
	if (!ValidDistort(tag)) return;

	bool extended = tag.Params.size() >= 8 && (!tag.Params[6].omitted || !tag.Params[7].omitted);
	if (!extended)
		state.corners[0] = Vector2D(0, 0);

	static size_t const corner_for_parameter[] = {1, 1, 2, 2, 3, 3, 0, 0};
	for (size_t parameter = 0; parameter < std::min<size_t>(tag.Params.size(), 8); ++parameter) {
		if (tag.Params[parameter].omitted)
			continue;
		size_t corner = corner_for_parameter[parameter];
		std::string raw_value = tag.Params[parameter].Get<std::string>();
		if (raw_value.empty()) continue; // Mangetsu retains omitted coordinate values.
		double current = parameter % 2 == 0 ? state.corners[corner].X() : state.corners[corner].Y();
		double value = raw_value.size() > 1 && raw_value[0] == '~' ?
			current + std::strtod(raw_value.c_str() + 1, nullptr) :
			std::strtod(raw_value.c_str(), nullptr);
		state.corners[corner] = parameter % 2 == 0 ?
			Vector2D(static_cast<float>(value), state.corners[corner].Y()) :
			Vector2D(state.corners[corner].X(), static_cast<float>(value));
	}
	state.enabled = true;
	state.source_eight_values = extended;
}

bool IsFinite(MangetsuDistortState const& state) {
	for (auto const& corner : state.corners)
		if (!std::isfinite(corner.X()) || !std::isfinite(corner.Y()))
			return false;
	return true;
}

std::string FormatCoordinate(double value) {
	if (std::abs(value) < 0.0000005)
		value = 0;
	return float_to_string(value, 6);
}
}

MangetsuDistortState GetMangetsuDistort(AssDialogue const& line) {
	MangetsuDistortState state;
	auto blocks = line.ParseTags();
	size_t begin = 0;
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		size_t end = BlockEnd(*block, line.Text.get(), begin);
		if (block->GetType() == AssBlockType::OVERRIDE)
			for (auto const& tag : SourceTags(line.Text.get(), begin + 1, end - 1))
				ApplyTag(state, tag.tag);
		begin = end;
	}
	return state;
}

std::string GetMangetsuDistortUnitText(AssDialogue const& line) {
	auto const& source = line.Text.get();
	auto blocks = line.ParseTags();
	MangetsuDistortState state;
	std::map<std::string, std::string> authored_style;
	bool found_text = false;
	size_t begin = 0;
	for (auto const& block : blocks) {
		size_t end = BlockEnd(*block, source, begin);
		if (block->GetType() == AssBlockType::DRAWING) {
			// Drawings are independent domains, even with the eight-value form.
			return source.substr(0, found_text ? begin : end);
		}
		if (block->GetType() == AssBlockType::OVERRIDE) {
			for (auto const& entry : SourceTags(source, begin + 1, end - 1)) {
				auto const& tag = entry.tag;
				auto next = state;
				ApplyTag(next, tag);
				if (tag.Name == "\\r") authored_style.clear();
				bool run_change = false;
				if (ChangesRenderRun(tag)) {
					std::string value = tag;
					auto previous = authored_style.find(tag.Name);
					run_change = previous == authored_style.end() || previous->second != value;
					authored_style[tag.Name] = value;
				}
				if (found_text && (tag.Name == "\\r" ||
					(ValidDistort(tag) && (next.corners != state.corners ||
						next.source_eight_values != state.source_eight_values || next.enabled != state.enabled)) ||
					run_change))
					return source.substr(0, begin);
				state = next;
			}
		}
		else if (IsRenderable(*block)) found_text = true;
		// Spaces, NBSP and hard breaks remain inside the shared source domain.
		begin = end;
	}
	return source;
}

std::string FormatMangetsuDistort(MangetsuDistortState const& state) {
	std::string result = "(";
	for (size_t corner : {1u, 2u, 3u, 0u}) {
		if (result.size() > 1)
			result += ',';
		result += FormatCoordinate(state.corners[corner].X());
		result += ',';
		result += FormatCoordinate(state.corners[corner].Y());
	}
	return result + ')';
}

std::string GetMangetsuDistortMeasurementText(AssDialogue const& line, int wrap_style) {
	auto const& source = line.Text.get();
	auto blocks = line.ParseTags();
	std::string result;
	size_t begin = 0;
	for (auto const& block : blocks) {
		size_t end = BlockEnd(*block, source, begin);
		if (block->GetType() == AssBlockType::OVERRIDE) {
			for (auto const& entry : SourceTags(source, begin + 1, end - 1))
				if (entry.tag.Name == "\\q" && entry.tag.IsValid())
					wrap_style = entry.tag.Params[0].Get<int>(wrap_style);
		}
		if (block->GetType() != AssBlockType::PLAIN) result.append(source, begin, end - begin);
		else {
			for (size_t i = begin; i < end; ++i) {
				if (source[i] == '\r' || source[i] == '\n') {
					if (source[i] == '\r' && i + 1 < end && source[i + 1] == '\n') ++i;
					result += "\\N";
				}
				else if (source[i] == '\\' && i + 1 < end && source[i + 1] == 'h') {
					result += "\xC2\xA0";
					++i;
				}
				else if (source[i] == '\\' && i + 1 < end && source[i + 1] == 'n') {
					result += wrap_style == 2 ? "\\N" : " ";
					++i;
				}
				else result += source[i];
			}
		}
		begin = end;
	}
	return result;
}

bool SetMangetsuDistortCorner(MangetsuDistortState& state, size_t corner, Vector2D position) {
	if (corner >= state.corners.size() || !std::isfinite(position.X()) || !std::isfinite(position.Y()))
		return false;
	state.corners[corner] = position;
	state.enabled = true;
	return true;
}

bool SetMangetsuDistort(AssDialogue& line, MangetsuDistortState const& state, int changed_corner) {
	if (!IsFinite(state))
		return false;

	auto blocks = line.ParseTags();
	auto source = line.Text.get();
	size_t begin = 0, insertion = std::string::npos;
	size_t effective_begin = std::string::npos, effective_end = 0;
	bool explicit_p0 = false;
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		size_t end = BlockEnd(*block, source, begin);
		if (block->GetType() == AssBlockType::OVERRIDE) {
			insertion = end - 1;
			for (auto const& entry : SourceTags(source, begin + 1, end - 1)) {
				auto const& tag = entry.tag;
				if (tag.Name == "\\r") effective_begin = std::string::npos;
				else if (ValidDistort(tag)) {
					effective_begin = entry.begin;
					effective_end = entry.end;
					// Prototypes allocate eight Params even for six-value input.
					explicit_p0 = tag.Params.size() >= 8 && !tag.Params[6].omitted && !tag.Params[7].omitted &&
						source[entry.end - 1] == ')';
				}
			}
		}
		begin = end;
	}

	std::string tag_text = "\\distort" + FormatMangetsuDistort(state);
	if (effective_begin != std::string::npos && explicit_p0 && changed_corner >= 0 && changed_corner < 4) {
		static size_t const first_parameter_for_corner[] = {6, 0, 2, 4};
		size_t parameter = first_parameter_for_corner[changed_corner];
		size_t pair_begin = source.find('(', effective_begin) + 1;
		for (size_t i = 0; i < parameter; ++i) pair_begin = source.find(',', pair_begin) + 1;
		size_t pair_end = source.find(',', source.find(',', pair_begin) + 1);
		if (parameter == 6) pair_end = effective_end - 1;
		source.replace(pair_begin, pair_end - pair_begin,
			FormatCoordinate(state.corners[changed_corner].X()) + ',' + FormatCoordinate(state.corners[changed_corner].Y()));
	}
	else if (effective_begin != std::string::npos)
		source.replace(effective_begin, effective_end - effective_begin, tag_text);
	else if (insertion != std::string::npos)
		source.insert(insertion, tag_text);
	else source.insert(0, '{' + tag_text + '}');
	line.Text = source;
	return true;
}

std::vector<Vector2D> DistortQuadToScreen(
	std::vector<Vector2D> const& undistorted_screen_quad,
	MangetsuDistortState const& state)
{
	std::vector<Vector2D> result;
	result.reserve(4);
	for (auto const& corner : state.corners)
		result.push_back(UVToXY(undistorted_screen_quad, corner));
	return result;
}
