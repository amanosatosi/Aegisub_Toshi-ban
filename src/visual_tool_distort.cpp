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

#include "visual_tool_distort.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "mangetsu_distort.h"
#include "options.h"
#include "quad_geometry.h"
#include "selection_controller.h"
#include "vector3d.h"
#include "video_controller.h"

#include <algorithm>
#include <cmath>
#include <wx/colour.h>

VisualToolDistort::VisualToolDistort(VideoDisplay* parent, agi::Context* context)
: VisualTool<Feature>(parent, context)
{
	for (size_t i = 0; i < corners.size(); ++i) {
		corners[i] = new Feature(i);
		corners[i]->type = i == 0 ? DRAG_SMALL_SQUARE : DRAG_SMALL_CIRCLE;
		features.push_back(*corners[i]);
	}
	DoRefresh();
}

std::vector<Vector2D> VisualToolDistort::UndistortedQuad() {
	Vector2D pos = GetLinePosition(active_line);
	// Follow an ordinary move at the displayed frame. An explicit pos retains
	// the placement precedence used by the other visual tools.
	bool has_pos = false;
	for (auto const& block : active_line->ParseTags()) {
		if (block->GetType() != AssBlockType::OVERRIDE) continue;
		for (auto const& tag : static_cast<AssDialogueBlockOverride const&>(*block).Tags)
			if (tag.Name == "\\pos" && tag.IsValid()) has_pos = true;
	}
	Vector2D start, end;
	int t1, t2;
	if (!has_pos && GetLineMove(active_line, start, end, t1, t2)) {
		if (t1 <= 0 && t2 <= 0) {
			t1 = 0;
			t2 = active_line->End - active_line->Start;
		}
		int time = c->videoController->TimeAtFrame(frame_number) - active_line->Start;
		float progress = t2 > t1 ? std::max(0.f, std::min(1.f, float(time - t1) / (t2 - t1))) : float(time >= t1);
		pos = start + (end - start) * progress;
	}
	Vector2D org = GetLineOrigin(active_line);
	if (!org) org = pos;
	float rx, ry, rz, fax, fay;
	Vector2D scale;
	GetLineRotation(active_line, rx, ry, rz);
	GetLineShear(active_line, fax, fay);
	GetLineScale(active_line, scale);
	int align = GetLineAlignment(active_line);
	int wrap_style = c->ass->GetScriptInfoAsInt("WrapStyle");
	AssDialogue measured(*active_line);
	measured.Text = GetMangetsuDistortMeasurementText(*active_line, wrap_style);
	auto bbox = GetLineBaseExtents(&measured);
	float width = std::max(bbox.second.X() - bbox.first.X(), 1.f);
	float height = std::max(bbox.second.Y() - bbox.first.Y(), 1.f);
	Vector2D shift(-width * ((align - 1) % 3) / 2.f, -height * (2 - (align - 1) / 3) / 2.f);

	// The complete first source block is measured together: its widest visual
	// line and total height define one domain, rather than one quad per word.
	AssDialogue unit(*active_line);
	unit.Text = GetMangetsuDistortUnitText(*active_line);
	unit.Text = GetMangetsuDistortMeasurementText(unit, wrap_style);
	auto unit_bbox = GetLineBaseExtents(&unit);
	auto result = MakeRect(unit_bbox.first, unit_bbox.second);
	constexpr float deg2rad = 3.14159265358979323846f / 180.f;
	float screen_z = 312.5f * script_res.Y() / layout_res.Y();
	for (auto& point : result) {
		// Retain the old tool's normal ASS shear/scale/rotation/projection math.
		point = Vector2D(point.X() + point.Y() * fax, point.X() * fay + point.Y());
		point = (point + shift) * scale / 100.f + pos - org;
		Vector3D q(point);
		q = q.RotateZ(-rz * deg2rad).RotateX(-rx * deg2rad).RotateY(ry * deg2rad);
		q = (screen_z / (q.Z() + screen_z)) * q;
		point = FromScriptCoords(q.XY() + org);
	}
	return result;
}

void VisualToolDistort::DoRefresh() {
	if (!active_line) return;
	base_quad = UndistortedQuad();
	auto quad = DistortQuadToScreen(base_quad, GetMangetsuDistort(*active_line));
	for (size_t i = 0; i < corners.size(); ++i) corners[i]->pos = quad[i];
}

void VisualToolDistort::Draw() {
	if (!active_line) return;
	gl.SetLineColour(to_wx(line_color_primary_opt->GetColor()));
	for (size_t i = 0; i < corners.size(); ++i)
		gl.DrawDashedLine(corners[i]->pos, corners[(i + 1) % 4]->pos, 6);
	DrawAllFeatures();
}

bool VisualToolDistort::WriteCorner(Feature* feature) {
	if (!active_line || !feature || base_quad.size() != 4) return false;
	Vector2D normalized = XYToUV(base_quad, feature->pos);
	if (!std::isfinite(normalized.X()) || !std::isfinite(normalized.Y())) return false;
	bool changed = false;
	auto edit = [&](AssDialogue* line) {
		if (FilterLockedLines() && IsLockedLine(line)) return;
		// Preserve each selected line's own other corners, including legacy tags.
		auto state = GetMangetsuDistort(*line);
		if (SetMangetsuDistortCorner(state, feature->index, normalized))
			changed |= SetMangetsuDistort(*line, state, static_cast<int>(feature->index));
	};
	auto const& selected = c->selectionController->GetSelectedSet();
	if (selected.empty()) edit(active_line);
	else for (auto line : selected) edit(line);
	return changed;
}

void VisualToolDistort::UpdateDrag(Feature* feature) {
	if (!WriteCorner(feature)) DoRefresh();
}

void VisualToolDistort::OnDoubleClick() {
	if (!active_line) return;
	auto closest = *std::min_element(corners.begin(), corners.end(), [&](Feature* a, Feature* b) {
		return (a->pos - mouse_pos).Len() < (b->pos - mouse_pos).Len();
	});
	closest->pos = mouse_pos;
	if (WriteCorner(closest)) Commit();
	else DoRefresh();
}
