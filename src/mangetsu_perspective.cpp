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
	if (state.plane) {
		for (double value : state.matrix)
			if (!std::isfinite(value)) return false;
		return true;
	}
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
	if (tag.Name != "\\perspective" || !tag.IsValid() ||
		(tag.Params.size() != 8 && tag.Params.size() != 9))
		return;

	MangetsuPerspectiveState parsed = state;
	bool plane = tag.Params.size() == 9 && !tag.Params[8].omitted;
	for (size_t parameter = 0; parameter < (plane ? 9u : 8u); ++parameter) {
		if (tag.Params[parameter].omitted)
			return;
		std::string raw = tag.Params[parameter].Get<std::string>();
		char* end = nullptr;
		double current = plane ? (parameter < 8 ? parsed.matrix[parameter] : 1) :
			(parameter & 1 ? parsed.corners[parameter / 2].Y() : parsed.corners[parameter / 2].X());
		bool relative = raw.size() > 1 && raw[0] == '~';
		char const* number = raw.c_str() + (relative ? 1 : 0);
		double parsed_number = std::strtod(number, &end);
		double value = relative ? current + parsed_number : parsed_number;
		if (!end || end == number || *end || !std::isfinite(value))
			return;
		if (plane) {
			if (parameter == 8) {
				if (value != 1) return;
			} else parsed.matrix[parameter] = value;
		} else parsed.corners[parameter / 2] = parameter & 1 ?
			Vector2D(parsed.corners[parameter / 2].X(), static_cast<float>(value)) :
			Vector2D(static_cast<float>(value), parsed.corners[parameter / 2].Y());
	}
	parsed.plane = plane;
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
	if (state.plane) {
		for (double value : state.matrix) {
			if (result.size() > 1) result += ',';
			result += float_to_string(value, 9);
		}
		return result + ",1)";
	}
	for (auto const& corner : state.corners) {
		if (result.size() > 1)
			result += ',';
		result += FormatCoordinate(corner.X());
		result += ',';
		result += FormatCoordinate(corner.Y());
	}
	return result + ')';
}

Vector2D MangetsuPerspectiveReferenceCorner(size_t corner) {
	static const Vector2D reference[4] = {
		Vector2D(-100, -50), Vector2D(100, -50),
		Vector2D(100, 50), Vector2D(-100, 50)
	};
	return reference[corner % 4];
}

Vector2D MapMangetsuPerspective(MangetsuPerspectiveState const& state, Vector2D local) {
	if (!state.plane) return local;
	double x = local.X(), y = local.Y();
	double w = state.matrix[6] * x + state.matrix[7] * y + 1;
	if (!std::isfinite(w) || std::abs(w) < 1e-12) return local;
	return Vector2D(static_cast<float>((state.matrix[0] * x + state.matrix[1] * y + state.matrix[2]) / w),
		static_cast<float>((state.matrix[3] * x + state.matrix[4] * y + state.matrix[5]) / w));
}

bool SolveMangetsuPerspectivePlane(MangetsuPerspectiveState& state) {
	// Solve a projectivity from the fixed 200x100 local reference rectangle.
	// These handles are editor controls, never the current rendered text bounds.
	auto const& p = state.corners;
	double dx1 = p[1].X() - p[2].X(), dy1 = p[1].Y() - p[2].Y();
	double dx2 = p[3].X() - p[2].X(), dy2 = p[3].Y() - p[2].Y();
	double dx3 = p[0].X() - p[1].X() + p[2].X() - p[3].X();
	double dy3 = p[0].Y() - p[1].Y() + p[2].Y() - p[3].Y();
	double det = dx1 * dy2 - dx2 * dy1;
	if (!std::isfinite(det) || std::abs(det) < 1e-9) return false;
	double g = (dx3 * dy2 - dx2 * dy3) / det;
	double h = (dx1 * dy3 - dx3 * dy1) / det;
	if (!std::isfinite(g) || !std::isfinite(h) ||
		1 + g <= 1e-9 || 1 + h <= 1e-9 || 1 + g + h <= 1e-9) return false;
	double a = p[1].X() - p[0].X() + g * p[1].X();
	double b = p[3].X() - p[0].X() + h * p[3].X();
	double c = p[1].Y() - p[0].Y() + g * p[1].Y();
	double d = p[3].Y() - p[0].Y() + h * p[3].Y();
	state.matrix = {{a / 200, b / 100,
		p[0].X() + a / 2 + b / 2,
		c / 200, d / 100,
		p[0].Y() + c / 2 + d / 2,
		g / 200, h / 100}};
	// The unit homography must be divided by its denominator at local (0,0).
	double z = 1 + g / 2 + h / 2;
	if (!std::isfinite(z) || std::abs(z) < 1e-9) return false;
	// The above coefficients are from the unnormalized unit mapping after
	// substituting u=(x+100)/200 and v=(y+50)/100.
	for (double& value : state.matrix) value /= z;
	state.plane = true;
	state.enabled = true;
	return IsFinite(state);
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
	if (!state.plane && effective_tag && changed_corner >= 0 && changed_corner < 4 &&
		(effective_tag->Params.size() == 8 ||
		 (effective_tag->Params.size() == 9 && effective_tag->Params[8].omitted))) {
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
