// SPDX-License-Identifier: ISC
#pragma once

#include "vector2d.h"

#include <array>

class AssDialogue;

using MangetsuPerspectivePins = std::array<Vector2D, 4>;

bool HasAnimatedMangetsuPerspective(AssDialogue const& line);
bool SetMangetsuPerspective(AssDialogue& line, MangetsuPerspectivePins const& pins);
