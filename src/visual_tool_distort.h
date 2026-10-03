// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include "visual_feature.h"
#include "visual_tool.h"

#include <array>
#include <cstddef>
#include <vector>

class VisualToolDistortDraggableFeature final : public VisualDraggableFeature {
public:
	size_t index;
	explicit VisualToolDistortDraggableFeature(size_t index) : index(index) { }
};

class VisualToolDistort final : public VisualTool<VisualToolDistortDraggableFeature> {
	std::array<Feature*, 4> corners;
	std::vector<Vector2D> base_quad;

	// Read-only normal ASS geometry; never writes rotation/shear/scale/org.
	std::vector<Vector2D> UndistortedQuad();
	bool WriteCorner(Feature* feature);
	void DoRefresh() override;
	void OnFrameChanged() override { DoRefresh(); }
	void Draw() override;
	void OnDoubleClick() override;
	void UpdateDrag(Feature* feature) override;

public:
	VisualToolDistort(VideoDisplay* parent, agi::Context* context);
};
