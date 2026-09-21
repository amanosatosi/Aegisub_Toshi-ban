// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#pragma once

#include "vector2d.h"

#include <array>
#include <cstddef>
#include <string>

class AssDialogue;

struct MangetsuPerspectiveState {
	// Renderer/tag order: P0 top-left, P1 top-right, P2 bottom-right, P3 bottom-left.
	std::array<Vector2D, 4> corners {{
		Vector2D(), Vector2D(), Vector2D(), Vector2D()
	}};
	bool enabled = false;
};

MangetsuPerspectiveState GetMangetsuPerspective(AssDialogue const& line);
std::string FormatMangetsuPerspective(MangetsuPerspectiveState const& state);
bool SetMangetsuPerspectiveCorner(MangetsuPerspectiveState& state, size_t corner, Vector2D position);
bool SetMangetsuPerspective(AssDialogue& line, MangetsuPerspectiveState const& state, int changed_corner = -1);
