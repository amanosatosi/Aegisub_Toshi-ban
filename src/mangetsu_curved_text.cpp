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
#include <sstream>
#include <vector>

namespace {
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

void ApplyTag(MangetsuCurvedTextState& state, AssOverrideTag const& tag) {
	if (tag.Name == "\\r") {
		state = MangetsuCurvedTextState();
		return;
	}
	if (tag.Name == "\\ct" && tag.IsValid() && !tag.Params.empty() && !tag.Params[0].omitted) {
		state.path = tag.Params[0].Get<std::string>();
		state.has_path = !state.path.empty();
	}
	else if (tag.Name == "\\ctx" && tag.IsValid()) {
		state.along_offset = GetNumber(tag, 0.0);
		state.has_along_offset = true;
	}
	else if (tag.Name == "\\cty" && tag.IsValid()) {
		state.normal_offset = GetNumber(tag, 0.0);
		state.has_normal_offset = true;
	}
	else if (tag.Name == "\\ctan" && tag.IsValid()) {
		int alignment = static_cast<int>(GetNumber(tag, 0.0));
		if (alignment >= 1 && alignment <= 3) {
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

std::string FormatNumber(double value) {
	if (!std::isfinite(value))
		return std::string();
	if (std::abs(value) < 0.0000005)
		value = 0.0;
	return float_to_string(value, 6);
}
}

MangetsuCurvedTextState GetMangetsuCurvedText(AssDialogue const& line) {
	MangetsuCurvedTextState state;
	auto blocks = line.ParseTags();
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		for (auto const& tag : static_cast<AssDialogueBlockOverride const&>(*block).Tags)
			ApplyTag(state, tag);
	}
	return state;
}

bool IsSupportedMangetsuCurvedTextPath(std::string const& path) {
	std::istringstream input(path);
	std::string token;
	char command = 0;
	int coordinate_count = 0;
	bool saw_move = false;
	auto valid_count = [&]() {
		if (!command)
			return coordinate_count == 0;
		if (command == 'm' || command == 'l')
			return coordinate_count >= 2 && coordinate_count % 2 == 0;
		return command == 'b' && coordinate_count >= 6 && coordinate_count % 6 == 0;
	};

	while (input >> token) {
		if (token.size() == 1 && std::isalpha(static_cast<unsigned char>(token[0]))) {
			if (!valid_count())
				return false;
			command = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
			if (command != 'm' && command != 'l' && command != 'b')
				return false;
			if (!saw_move && command != 'm')
				return false;
			coordinate_count = 0;
			continue;
		}
		double value = 0.0;
		if (!command || !agi::util::try_parse(token, &value) || !std::isfinite(value))
			return false;
		++coordinate_count;
		if (command == 'm' && coordinate_count >= 2)
			saw_move = true;
	}
	return saw_move && valid_count();
}

bool SetMangetsuCurvedTextPath(AssDialogue& line, std::string const& path) {
	if (!IsSupportedMangetsuCurvedTextPath(path))
		return false;
	return SetStaticTag(line, "\\ct", "\\ct(" + path + ")");
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
	if (value < 1 || value > 3)
		return false;
	return SetStaticTag(line, "\\ctan", "\\ctan" + std::to_string(value));
}
