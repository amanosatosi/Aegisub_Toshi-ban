// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "mangetsu_distort.h"

#include "ass_dialogue.h"
#include "perspective_geometry.h"
#include "utils.h"

#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {
bool HasRenderableText(std::string const& text) {
	for (size_t position = 0; position < text.size();) {
		if (text[position] == ' ' || text[position] == '\r' || text[position] == '\n') {
			++position;
			continue;
		}
		if (position + 1 < text.size() && text[position] == '\\' && (text[position + 1] == 'N' || text[position + 1] == 'n')) {
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
	if (tag.Name != "\\distort" || !tag.IsValid() || tag.Params.size() < 6)
		return;
	if (tag.Params.size() >= 8 && !tag.Params[6].omitted && tag.Params[7].omitted)
		return; // A lone seventh value is not a valid P0 pair.

	bool extended = tag.Params.size() >= 8 && (!tag.Params[6].omitted || !tag.Params[7].omitted);
	if (!extended)
		state.corners[0] = Vector2D(0, 0);

	static size_t const corner_for_parameter[] = {1, 1, 2, 2, 3, 3, 0, 0};
	for (size_t parameter = 0; parameter < std::min<size_t>(tag.Params.size(), 8); ++parameter) {
		if (tag.Params[parameter].omitted)
			continue;
		size_t corner = corner_for_parameter[parameter];
		std::string raw_value = tag.Params[parameter].Get<std::string>();
		double current = parameter % 2 == 0 ? state.corners[corner].X() : state.corners[corner].Y();
		double value = raw_value.size() > 1 && raw_value[0] == '~' ?
			current + std::strtod(raw_value.c_str() + 1, nullptr) :
			std::strtod(raw_value.c_str(), nullptr);
		state.corners[corner] = parameter % 2 == 0 ?
			Vector2D(static_cast<float>(value), state.corners[corner].Y()) :
			Vector2D(state.corners[corner].X(), static_cast<float>(value));
	}
	state.enabled = true;
	state.extended = extended;
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
	for (auto const& block : blocks) {
		if (IsRenderable(*block))
			break;
		if (block->GetType() != AssBlockType::OVERRIDE)
			continue;
		auto const& override_block = static_cast<AssDialogueBlockOverride const&>(*block);
		for (auto const& tag : override_block.Tags)
			ApplyTag(state, tag);
	}
	return state;
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

bool SetMangetsuDistortCorner(MangetsuDistortState& state, size_t corner, Vector2D position) {
	if (corner >= state.corners.size() || !std::isfinite(position.X()) || !std::isfinite(position.Y()))
		return false;
	state.corners[corner] = position;
	state.enabled = true;
	if (corner == 0)
		state.extended = true;
	return true;
}

bool SetMangetsuDistort(AssDialogue& line, MangetsuDistortState const& state, int changed_corner) {
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
			else if (tag.Name == "\\distort")
				effective_tag = &tag;
		}
	}

	std::string tag_text = "\\distort" + FormatMangetsuDistort(state);
	if (effective_tag && changed_corner >= 0 && changed_corner < 4 && effective_tag->Params.size() >= 8) {
		static size_t const first_parameter_for_corner[] = {6, 0, 2, 4};
		size_t parameter = first_parameter_for_corner[changed_corner];
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
