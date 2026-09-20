// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "vector2d.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

class AssDialogue;

struct MangetsuDistortState {
	// Renderer order: P0 top-left, P1 top-right, P2 bottom-right, P3 bottom-left.
	std::array<Vector2D, 4> corners {{
		Vector2D(0, 0), Vector2D(1, 0), Vector2D(1, 1), Vector2D(0, 1)
	}};
	bool enabled = false;
	bool extended = false;
};

MangetsuDistortState GetMangetsuDistort(AssDialogue const& line);
/// Build a copy of the first renderer-equivalent distortion unit. Internal
/// spaces and NBSP are retained; hard breaks, style/distort changes and
/// drawing chunks terminate the unit.
std::string GetMangetsuDistortUnitText(AssDialogue const& line);
std::string FormatMangetsuDistort(MangetsuDistortState const& state);
bool SetMangetsuDistortCorner(MangetsuDistortState& state, size_t corner, Vector2D position);
bool SetMangetsuDistort(AssDialogue& line, MangetsuDistortState const& state, int changed_corner = -1);

std::vector<Vector2D> DistortQuadToScreen(
	std::vector<Vector2D> const& undistorted_screen_quad,
	MangetsuDistortState const& state);
