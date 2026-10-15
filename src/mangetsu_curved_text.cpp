// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "mangetsu_curved_text.h"

#include "ass_dialogue.h"
#include "utils.h"

#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace {
bool IsRendererPathCandidate(std::string const& path) {
	MangetsuCurvedTextPath geometry;
	if (ParseMangetsuCurvedTextPath(path, geometry))
		return true;

	// The renderer's shared ASS drawing parser also accepts spline commands
	// which Aegisub's Spline class cannot round-trip. Recognise these
	// conservatively so a valid advanced path is never shadowed or replaced.
	std::istringstream input(path);
	std::string token;
	bool first = true;
	bool saw_advanced = false;
	while (input >> token) {
		if (token.size() == 1 && std::isalpha(static_cast<unsigned char>(token[0]))) {
			char command = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
			if (first && command != 'm')
				return false;
			if (std::string("mnlbspc").find(command) == std::string::npos)
				return false;
			saw_advanced |= command == 'n' || command == 's' || command == 'p' || command == 'c';
			first = false;
			continue;
		}
		double value = 0.0;
		if (first || !agi::util::try_parse(token, &value) || !std::isfinite(value))
			return false;
	}
	return saw_advanced;
}

bool HasRenderableText(std::string const& text) {
	for (size_t position = 0; position < text.size();) {
		unsigned char ch = static_cast<unsigned char>(text[position]);
		if (std::isspace(ch)) {
			++position;
			continue;
		}
		if (position + 1 < text.size() && text[position] == '\\' &&
			(text[position + 1] == 'N' || text[position + 1] == 'n'))
		{
			position += 2;
			continue;
		}
		if (position + 1 < text.size() && ch == 0xC2 &&
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

double GetNumber(AssOverrideTag const& tag, double fallback) {
	if (!tag.IsValid() || tag.Params.empty() || tag.Params[0].omitted)
		return fallback;
	return tag.Params[0].Get<double>();
}

int GetPathAnchor(AssOverrideTag const& tag) {
	if (!tag.IsValid() || tag.Params.empty() || tag.Params[0].omitted)
		return 0;
	auto raw = tag.Params[0].Get<std::string>();
	char *end = nullptr;
	long value = std::strtol(raw.c_str(), &end, 10);
	return end != raw.c_str() && *end == '\0' && value >= 1 && value <= 9 ?
		static_cast<int>(value) : 0;
}

void ApplyTag(MangetsuCurvedTextState& state, AssOverrideTag const& tag) {
	if (tag.Name == "\\r") {
		auto path = state.path;
		bool has_path = state.has_path;
		state = MangetsuCurvedTextState();
		state.path = std::move(path);
		state.has_path = has_path;
		return;
	}
	if (tag.Name == "\\ct" && !state.has_path && tag.IsValid() && !tag.Params.empty() && !tag.Params[0].omitted) {
		auto path = tag.Params[0].Get<std::string>();
		if (IsRendererPathCandidate(path)) {
			state.path = std::move(path);
			state.has_path = true;
		}
	}
	else if (tag.Name == "\\ctx" && tag.IsValid()) {
		state.along_offset = GetNumber(tag, 0.0);
		state.has_along_offset = true;
	}
	else if (tag.Name == "\\cty" && tag.IsValid()) {
		state.normal_offset = GetNumber(tag, 0.0);
		state.has_normal_offset = true;
	}
	else if (tag.Name == "\\ctan" && tag.IsValid() && !state.has_alignment) {
		int alignment = GetPathAnchor(tag);
		if (alignment) {
			state.alignment = alignment;
			state.has_alignment = true;
		}
	}
}

bool SetStaticTag(AssDialogue& line, std::string const& name, std::string const& text) {
	auto blocks = line.ParseTags();
	AssDialogueBlockOverride* insertion_block = nullptr;
	AssOverrideTag* effective_tag = nullptr;
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		auto& override_block = static_cast<AssDialogueBlockOverride&>(*block);
		insertion_block = &override_block;
		for (auto& tag : override_block.Tags) {
			if (tag.Name == "\\r")
				effective_tag = nullptr;
			else if (tag.Name == name)
				effective_tag = &tag;
		}
	}

	if (effective_tag)
		effective_tag->SetText(text);
	else if (insertion_block)
		insertion_block->AddTag(text);
	else {
		auto block = agi::make_unique<AssDialogueBlockOverride>();
		block->AddTag(text);
		blocks.insert(blocks.begin(), std::move(block));
	}
	line.UpdateText(blocks);
	return true;
}

bool SetStaticAlignmentTag(AssDialogue& line, int value) {
	auto blocks = line.ParseTags();
	AssDialogueBlockOverride* insertion_block = nullptr;
	AssOverrideTag* effective_tag = nullptr;
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		auto& override_block = static_cast<AssDialogueBlockOverride&>(*block);
		insertion_block = &override_block;
		for (auto& tag : override_block.Tags) {
			if (tag.Name == "\\r")
				effective_tag = nullptr;
			else if (tag.Name == "\\ctan" && tag.IsValid() && !effective_tag) {
				if (GetPathAnchor(tag))
					effective_tag = &tag;
			}
		}
	}

	std::string text = "\\ctan" + std::to_string(value);
	if (effective_tag)
		effective_tag->SetText(text);
	else if (insertion_block)
		insertion_block->AddTag(text);
	else {
		auto block = agi::make_unique<AssDialogueBlockOverride>();
		block->AddTag(text);
		blocks.insert(blocks.begin(), std::move(block));
	}
	line.UpdateText(blocks);
	return true;
}

bool SetStaticPathTag(AssDialogue& line, std::string const& path) {
	auto blocks = line.ParseTags();
	AssDialogueBlockOverride* insertion_block = nullptr;
	AssOverrideTag* effective_tag = nullptr;
	for (auto const& block : blocks) {
		if (!insertion_block && block->GetType() == AssBlockType::OVERRIDE)
			insertion_block = static_cast<AssDialogueBlockOverride*>(block.get());
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		for (auto& tag : static_cast<AssDialogueBlockOverride&>(*block).Tags) {
			if (tag.Name != "\\ct" || !tag.IsValid() || tag.Params.empty() || tag.Params[0].omitted)
				continue;
			if (IsRendererPathCandidate(tag.Params[0].Get<std::string>())) {
				effective_tag = &tag;
				break;
			}
		}
		if (effective_tag)
			break;
	}

	std::string text = "\\ct(" + path + ")";
	if (effective_tag)
		effective_tag->SetText(text);
	else if (insertion_block)
		insertion_block->AddTag(text);
	else {
		auto block = agi::make_unique<AssDialogueBlockOverride>();
		block->AddTag(text);
		blocks.insert(blocks.begin(), std::move(block));
	}
	line.UpdateText(blocks);
	return true;
}

std::string FormatNumber(double value) {
	if (!std::isfinite(value))
		return std::string();
	if (std::abs(value) < 0.0000005)
		value = 0.0;
	return float_to_string(value, 6);
}

bool Near(Vector2D left, Vector2D right, float epsilon = 0.01f) {
	return (left - right).SquareLen() <= epsilon * epsilon;
}
}

MangetsuCurvedTextState GetMangetsuCurvedText(AssDialogue const& line) {
	MangetsuCurvedTextState state;
	auto blocks = line.ParseTags();
	for (auto const& block : blocks) {
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		for (auto const& tag : static_cast<AssDialogueBlockOverride const&>(*block).Tags)
			ApplyTag(state, tag);
	}
	return state;
}

bool IsSupportedMangetsuCurvedTextPath(std::string const& path) {
	MangetsuCurvedTextPath geometry;
	return ParseMangetsuCurvedTextPath(path, geometry);
}

bool ParseMangetsuCurvedTextPath(std::string const& path, MangetsuCurvedTextPath& geometry) {
	geometry = MangetsuCurvedTextPath();
	std::istringstream input(path);
	std::string token;
	char command = 0;
	std::vector<double> coordinates;
	bool have_move = false;
	Vector2D current(0, 0);

	auto consume = [&]() {
		if (!command)
			return coordinates.empty();
		if (command == 'm') {
			if (have_move || coordinates.size() != 2)
				return false;
			geometry.start = Vector2D(static_cast<float>(coordinates[0]), static_cast<float>(coordinates[1]));
			current = geometry.start;
			have_move = true;
			return true;
		}
		if (!have_move || coordinates.empty())
			return false;
		size_t stride = command == 'l' ? 2 : 6;
		if (coordinates.size() % stride)
			return false;
		for (size_t index = 0; index < coordinates.size(); index += stride) {
			MangetsuCurvedTextSegment segment;
			segment.start = current;
			if (command == 'l') {
				segment.type = MangetsuCurvedTextSegmentType::LINE;
				segment.end = Vector2D(static_cast<float>(coordinates[index]), static_cast<float>(coordinates[index + 1]));
			}
			else {
				segment.type = MangetsuCurvedTextSegmentType::CUBIC;
				segment.control1 = Vector2D(static_cast<float>(coordinates[index]), static_cast<float>(coordinates[index + 1]));
				segment.control2 = Vector2D(static_cast<float>(coordinates[index + 2]), static_cast<float>(coordinates[index + 3]));
				segment.end = Vector2D(static_cast<float>(coordinates[index + 4]), static_cast<float>(coordinates[index + 5]));
			}
			geometry.segments.push_back(segment);
			current = segment.end;
		}
		return true;
	};

	while (input >> token) {
		if (token.size() == 1 && std::isalpha(static_cast<unsigned char>(token[0]))) {
			if (!consume())
				return false;
			command = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
			if (command != 'm' && command != 'l' && command != 'b')
				return false;
			coordinates.clear();
			continue;
		}
		double value = 0.0;
		if (!command || !agi::util::try_parse(token, &value) || !std::isfinite(value) ||
			value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max())
			return false;
		coordinates.push_back(value);
	}
	if (!consume() || !have_move || geometry.segments.empty())
		return false;
	return std::any_of(geometry.segments.begin(), geometry.segments.end(), [](MangetsuCurvedTextSegment const& segment) {
		return !Near(segment.start, segment.end) ||
			(segment.type == MangetsuCurvedTextSegmentType::CUBIC &&
			(!Near(segment.start, segment.control1) || !Near(segment.end, segment.control2)));
	});
}

std::string FormatMangetsuCurvedTextPath(MangetsuCurvedTextPath const& geometry) {
	if (geometry.segments.empty())
		return std::string();
	std::string result = "m " + geometry.start.Str(' ');
	char previous = 0;
	for (auto const& segment : geometry.segments) {
		char command = segment.type == MangetsuCurvedTextSegmentType::LINE ? 'l' : 'b';
		if (command != previous) {
			result += " ";
			result += command;
			previous = command;
		}
		if (segment.type == MangetsuCurvedTextSegmentType::LINE)
			result += " " + segment.end.Str(' ');
		else
			result += " " + segment.control1.Str(' ') + " " + segment.control2.Str(' ') + " " + segment.end.Str(' ');
	}
	return result;
}

bool GetMangetsuCurvedTextArc(std::string const& path, MangetsuCurvedTextArc& arc) {
	MangetsuCurvedTextPath geometry;
	if (!ParseMangetsuCurvedTextPath(path, geometry) || geometry.segments.size() != 1)
		return false;
	auto const& segment = geometry.segments.front();
	arc.start = geometry.start;
	arc.end = segment.end;
	if (segment.type == MangetsuCurvedTextSegmentType::LINE) {
		arc.bend = (arc.start + arc.end) / 2.f;
		return true;
	}
	Vector2D quadratic1 = arc.start + (segment.control1 - arc.start) * 1.5f;
	Vector2D quadratic2 = arc.end + (segment.control2 - arc.end) * 1.5f;
	if (!Near(quadratic1, quadratic2, 0.05f))
		return false;
	arc.bend = (arc.start + segment.control1 * 3.f + segment.control2 * 3.f + arc.end) / 8.f;
	return true;
}

std::string FormatMangetsuCurvedTextArc(MangetsuCurvedTextArc const& arc) {
	Vector2D midpoint = (arc.start + arc.end) / 2.f;
	if (Near(arc.bend, midpoint))
		return "m " + arc.start.Str(' ') + " l " + arc.end.Str(' ');
	Vector2D quadratic = arc.bend * 2.f - midpoint;
	Vector2D control1 = arc.start + (quadratic - arc.start) * (2.f / 3.f);
	Vector2D control2 = arc.end + (quadratic - arc.end) * (2.f / 3.f);
	return "m " + arc.start.Str(' ') + " b " + control1.Str(' ') + " " + control2.Str(' ') + " " + arc.end.Str(' ');
}

std::string MakeDefaultMangetsuCurvedTextPath(double width, int alignment) {
	float usable_width = static_cast<float>(std::max(50.0, std::isfinite(width) ? width : 50.0));
	int horizontal = (alignment - 1) % 3;
	MangetsuCurvedTextArc arc;
	if (horizontal == 0) {
		arc.start = Vector2D(0, 0);
		arc.end = Vector2D(usable_width, 0);
	}
	else if (horizontal == 2) {
		arc.start = Vector2D(-usable_width, 0);
		arc.end = Vector2D(0, 0);
	}
	else {
		arc.start = Vector2D(-usable_width / 2.f, 0);
		arc.end = Vector2D(usable_width / 2.f, 0);
	}
	arc.bend = (arc.start + arc.end) / 2.f;
	return FormatMangetsuCurvedTextArc(arc);
}

bool ResetMangetsuCurvedTextPathStraight(std::string const& path, std::string& result) {
	MangetsuCurvedTextPath geometry;
	if (!ParseMangetsuCurvedTextPath(path, geometry))
		return false;
	result = "m " + geometry.start.Str(' ') + " l " + geometry.segments.back().end.Str(' ');
	return true;
}

bool ReverseMangetsuCurvedTextPath(std::string const& path, std::string& result) {
	MangetsuCurvedTextPath geometry;
	if (!ParseMangetsuCurvedTextPath(path, geometry))
		return false;
	MangetsuCurvedTextPath reversed;
	reversed.start = geometry.segments.back().end;
	for (auto segment = geometry.segments.rbegin(); segment != geometry.segments.rend(); ++segment) {
		MangetsuCurvedTextSegment item;
		item.type = segment->type;
		item.start = segment->end;
		item.end = segment->start;
		if (item.type == MangetsuCurvedTextSegmentType::CUBIC) {
			item.control1 = segment->control2;
			item.control2 = segment->control1;
		}
		reversed.segments.push_back(item);
	}
	result = FormatMangetsuCurvedTextPath(reversed);
	return !result.empty();
}

bool SetMangetsuCurvedTextPath(AssDialogue& line, std::string const& path) {
	if (!IsSupportedMangetsuCurvedTextPath(path))
		return false;
	return SetStaticPathTag(line, path);
}

bool RemoveMangetsuCurvedTextPath(AssDialogue& line) {
	auto blocks = line.ParseTags();
	bool removed = false;
	for (auto const& block : blocks) {
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		auto& tags = static_cast<AssDialogueBlockOverride&>(*block).Tags;
		auto end = std::remove_if(tags.begin(), tags.end(), [&](AssOverrideTag const& tag) {
			if (tag.Name != "\\ct")
				return false;
			removed = true;
			return true;
		});
		tags.erase(end, tags.end());
	}
	if (removed) {
		blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](std::unique_ptr<AssDialogueBlock> const& block) {
			return block->GetType() == AssBlockType::OVERRIDE &&
				static_cast<AssDialogueBlockOverride const&>(*block).Tags.empty();
		}), blocks.end());
		line.UpdateText(blocks);
	}
	return removed;
}

bool SetMangetsuCurvedTextAlongOffset(AssDialogue& line, double value) {
	auto formatted = FormatNumber(value);
	return !formatted.empty() && SetStaticTag(line, "\\ctx", "\\ctx" + formatted);
}

bool SetMangetsuCurvedTextNormalOffset(AssDialogue& line, double value) {
	auto formatted = FormatNumber(value);
	return !formatted.empty() && SetStaticTag(line, "\\cty", "\\cty" + formatted);
}

bool SetMangetsuCurvedTextAlignment(AssDialogue& line, int value) {
	if (value < 1 || value > 9)
		return false;
	return SetStaticAlignmentTag(line, value);
}

bool RemoveMangetsuCurvedTextAlignment(AssDialogue& line) {
	auto blocks = line.ParseTags();
	bool removed = false;
	for (auto const& block : blocks) {
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		auto& tags = static_cast<AssDialogueBlockOverride&>(*block).Tags;
		auto end = std::remove_if(tags.begin(), tags.end(), [&](AssOverrideTag const& tag) {
			if (tag.Name != "\\ctan")
				return false;
			removed = true;
			return true;
		});
		tags.erase(end, tags.end());
	}
	if (removed) {
		blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](std::unique_ptr<AssDialogueBlock> const& block) {
			return block->GetType() == AssBlockType::OVERRIDE &&
				static_cast<AssDialogueBlockOverride const&>(*block).Tags.empty();
		}), blocks.end());
		line.UpdateText(blocks);
	}
	return removed;
}
