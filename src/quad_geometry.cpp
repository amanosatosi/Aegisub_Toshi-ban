// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "quad_geometry.h"

namespace {
void UnwrapQuadRel(std::vector<Vector2D> const& quad, float& x1, float& x2, float& x3, float& x4, float& y1, float& y2, float& y3, float& y4) {
	x1 = quad[0].X();
	x2 = quad[1].X() - x1;
	x3 = quad[2].X() - x1;
	x4 = quad[3].X() - x1;
	y1 = quad[0].Y();
	y2 = quad[1].Y() - y1;
	y3 = quad[2].Y() - y1;
	y4 = quad[3].Y() - y1;
}
}

Vector2D XYToUV(std::vector<Vector2D> const& quad, Vector2D xy) {
	float x1, x2, x3, x4, y1, y2, y3, y4;
	UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
	float x = xy.X() - x1;
	float y = xy.Y() - y1;
	// Exact inverse of UVToXY's projective quad mapping.
	float u = -(((x3*y2 - x2*y3)*(x4*y - x*y4)*(x4*(-y2 + y3) + x3*(y2 - y4) + x2*(-y3 + y4)))/(x3*x3*(x4*y2*y2*(-y + y4) + y4*(x*y2*(y2 - y4) + x2*(y - y2)*y4)) + x3*(x4*x4*y2*y2*(y - y3) + 2*x4*(x2*y*y3*(y2 - y4) + x*y2*(-y2 + y3)*y4) + x2*y4*(x2*(-y + y3)*y4 + 2*x*y2*(-y3 + y4))) + y3*(x*x4*x4*y2*(y2 - y3) + x2*x4*x4*(y2*y3 + y*(-2*y2 + y3)) - x2*x2*(x4*y*(y3 - 2*y4) + x4*y3*y4 + x*y4*(-y3 + y4)))));
	float v = ((x2*y - x*y2)*(x4*y3 - x3*y4)*(x4*(y2 - y3) + x2*(y3 - y4) + x3*(-y2 + y4)))/(x3*(x4*x4*y2*y2*(-y + y3) + x2*y4*(2*x*y2*(y3 - y4) + x2*(y - y3)*y4) - 2*x4*(x2*y*y3*(y2 - y4) + x*y2*(-y2 + y3)*y4)) + x3*x3*(x4*y2*y2*(y - y4) + y4*(x2*(-y + y2)*y4 + x*y2*(-y2 + y4))) + y3*(x*x4*x4*y2*(-y2 + y3) + x2*x4*x4*(2*y*y2 - y*y3 - y2*y3) + x2*x2*(x4*y*(y3 - 2*y4) + x4*y3*y4 + x*y4*(-y3 + y4))));
	return Vector2D(u, v);
}

Vector2D UVToXY(std::vector<Vector2D> const& quad, Vector2D uv) {
	float x1, x2, x3, x4, y1, y2, y3, y4;
	UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
	float u = uv.X();
	float v = uv.Y();
	float d = (x4*((-1 + u + v)*y2 + y3 - v*y3) + x3*(y2 - u*y2 + (-1 + v)*y4) + x2*((-1 + u)*y3 - (-1 + u + v)*y4));
	float x = (v*x4*(x3*y2 - x2*y3) + u*x2*(x4*y3 - x3*y4)) / d;
	float y = (v*y4*(x3*y2 - x2*y3) + u*y2*(x4*y3 - x3*y4)) / d;
	return Vector2D(x + x1, y + y1);
}

std::vector<Vector2D> MakeRect(Vector2D a, Vector2D b) {
	return {
		Vector2D(a.X(), a.Y()),
		Vector2D(b.X(), a.Y()),
		Vector2D(b.X(), b.Y()),
		Vector2D(a.X(), b.Y()),
	};
}
