// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "vector2d.h"

#include <vector>

void Solve2x2(float a11, float a12, float a21, float a22, float b1, float b2, float& x1, float& x2);
Vector2D QuadMidpoint(std::vector<Vector2D> const& quad);
Vector2D XYToUV(std::vector<Vector2D> const& quad, Vector2D xy);
Vector2D UVToXY(std::vector<Vector2D> const& quad, Vector2D uv);
std::vector<Vector2D> MakeRect(Vector2D a, Vector2D b);
