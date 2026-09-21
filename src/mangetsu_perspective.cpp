// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#include "mangetsu_perspective.h"

#include "ass_dialogue.h"
#include "utils.h"

#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {
bool HasRenderableText(std::string const& text) {
	return std::any_of(text.begin(), text.end(), [](unsigned char c) {
		return c != ' ' && c != '\t' && c != '\r' && c != '\n';
	});
}

bool IsRenderable(AssDialogueBlock const& block) {
	if (block.GetType() == AssBlockType::PLAIN)
		return HasRenderableText(static_cast<AssDialogueBlockPlain const&>(block).text);
	if (block.GetType() == AssBlockType::DRAWING)
		return !static_cast<AssDialogueBlockDrawing const&>(block).text.empty();
	return false;
}

bool IsFinite(MangetsuPerspectiveState const& state) {
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

void ApplyTag(MangetsuPerspectiveState& state, AssOverrideTag const& tag) {
	if (tag.Name == "\\r") {
		state = MangetsuPerspectiveState();
		return;
	}
	if (tag.Name != "\\perspective" || !tag.IsValid() || tag.Params.size() != 8)
		return;

	MangetsuPerspectiveState parsed = state;
	for (size_t parameter = 0; parameter < 8; ++parameter) {
		if (tag.Params[parameter].omitted)
			return;
		std::string raw = tag.Params[parameter].Get<std::string>();
		char* end = nullptr;
		double current = parameter & 1 ? parsed.corners[parameter / 2].Y() : parsed.corners[parameter / 2].X();
		double value = raw.size() > 1 && raw[0] == '~' ?
			current + std::strtod(raw.c_str() + 1, &end) : std::strtod(raw.c_str(), &end);
		if (!end || *end || !std::isfinite(value))
			return;
		parsed.corners[parameter / 2] = parameter & 1 ?
			Vector2D(parsed.corners[parameter / 2].X(), static_cast<float>(value)) :
			Vector2D(static_cast<float>(value), parsed.corners[parameter / 2].Y());
	}
	parsed.enabled = true;
	state = parsed;
}
}

MangetsuPerspectiveState GetMangetsuPerspective(AssDialogue const& line) {
	MangetsuPerspectiveState state;
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

std::string FormatMangetsuPerspective(MangetsuPerspectiveState const& state) {
	std::string result = "(";
	for (auto const& corner : state.corners) {
		if (result.size() > 1)
			result += ',';
		result += FormatCoordinate(corner.X());
		result += ',';
		result += FormatCoordinate(corner.Y());
	}
	return result + ')';
}

bool SetMangetsuPerspectiveCorner(MangetsuPerspectiveState& state, size_t corner, Vector2D position) {
	if (corner >= state.corners.size() || !std::isfinite(position.X()) || !std::isfinite(position.Y()))
		return false;
	state.corners[corner] = position;
	state.enabled = true;
	return true;
}

bool SetMangetsuPerspective(AssDialogue& line, MangetsuPerspectiveState const& state, int changed_corner) {
	if (!IsFinite(state))
		return false;

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
			else if (tag.Name == "\\perspective")
				effective_tag = &tag;
		}
	}

	std::string tag_text = "\\perspective" + FormatMangetsuPerspective(state);
	if (effective_tag && changed_corner >= 0 && changed_corner < 4 && effective_tag->Params.size() == 8) {
		size_t parameter = static_cast<size_t>(changed_corner) * 2;
		effective_tag->Params[parameter].Set<std::string>(FormatCoordinate(state.corners[changed_corner].X()));
		effective_tag->Params[parameter + 1].Set<std::string>(FormatCoordinate(state.corners[changed_corner].Y()));
	}
	else if (effective_tag)
		effective_tag->SetText(tag_text);
	else if (insertion_block)
		insertion_block->AddTag(tag_text);
	else {
		auto block = agi::make_unique<AssDialogueBlockOverride>();
		block->AddTag(tag_text);
		blocks.insert(blocks.begin(), std::move(block));
	}
	line.UpdateText(blocks);
	return true;
}
