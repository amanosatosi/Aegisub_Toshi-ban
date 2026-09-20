// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "visual_tool_curved_text.h"

#include "ass_dialogue.h"
#include "command/command.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"

#include <libaegisub/make_unique.h>

#include <algorithm>
#include <boost/algorithm/string/trim.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/set_algorithm.hpp>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>
#include <wx/colour.h>
#include <wx/toolbar.h>

namespace {
int const BUTTON_ID_BASE = 1500;
int const BUTTON_ID_ALIGNMENT = 1510;

bool InBox(Vector2D top_left, Vector2D bottom_right, Vector2D point) {
	return point.X() >= top_left.X() && point.X() <= bottom_right.X() &&
		point.Y() >= top_left.Y() && point.Y() <= bottom_right.Y();
}
}

VisualToolCurvedText::VisualToolCurvedText(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualToolCurvedTextDraggableFeature>(parent, context)
{
	spline.SetScale(1);
	feature_size = OPT_GET("Tool/Visual/Shape Handle Size")->GetInt();
}

Vector2D VisualToolCurvedText::LocalToScreen(Vector2D point) const {
	return FromScriptCoords(anchor + point);
}

Vector2D VisualToolCurvedText::ScreenToLocal(Vector2D point) const {
	return ToScriptCoords(point) - anchor;
}

std::string VisualToolCurvedText::EncodePath() const {
	return boost::trim_copy(spline.EncodeToAss());
}

bool VisualToolCurvedText::EnsurePathTag() {
	if (!active_line || !editable)
		return false;
	if (state.has_path)
		return true;
	if (!SetMangetsuCurvedTextPath(*active_line, EncodePath()))
		return false;
	state.has_path = true;
	state.path = EncodePath();
	provisional = false;
	return true;
}

bool VisualToolCurvedText::SavePath() {
	if (!active_line || !editable)
		return false;
	auto path = EncodePath();
	if (!SetMangetsuCurvedTextPath(*active_line, path))
		return false;
	state.path = path;
	state.has_path = true;
	provisional = false;
	return true;
}

void VisualToolCurvedText::AddTool(std::string const& command_name, int id) {
	auto command = cmd::get(command_name);
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolBar->AddTool(BUTTON_ID_BASE + id, command->StrDisplay(c), command->Icon(icon_size), command->GetTooltip("Video"), wxITEM_CHECK);
}

void VisualToolCurvedText::SetToolbar(wxToolBar *toolbar) {
	toolBar = toolbar;
	toolBar->AddSeparator();
	AddTool("video/tool/curved_text/edit", CT_EDIT_PATH);
	AddTool("video/tool/curved_text/move", CT_MOVE_PATH);
	AddTool("video/tool/curved_text/ctx", CT_ALONG_OFFSET);
	AddTool("video/tool/curved_text/cty", CT_NORMAL_OFFSET);
	toolBar->AddSeparator();
	auto alignment = cmd::get("video/tool/curved_text/ctan");
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolBar->AddTool(BUTTON_ID_ALIGNMENT, alignment->StrDisplay(c), alignment->Icon(icon_size), alignment->GetTooltip("Video"));
	toolBar->Bind(wxEVT_TOOL, &VisualToolCurvedText::OnSubTool, this);
	toolBar->Realize();
	toolBar->Show(true);
	SetSubTool(CT_EDIT_PATH);
}

void VisualToolCurvedText::OnSubTool(wxCommandEvent& event) {
	if (event.GetId() == BUTTON_ID_ALIGNMENT)
		CycleAlignment();
	else
		SetSubTool(event.GetId() - BUTTON_ID_BASE);
}

void VisualToolCurvedText::SetSubTool(int subtool) {
	if (subtool == CT_CYCLE_ALIGNMENT) {
		CycleAlignment();
		return;
	}
	if (!toolBar || subtool < CT_EDIT_PATH || subtool >= CT_MODE_LAST)
		return;
	for (int value = CT_EDIT_PATH; value < CT_MODE_LAST; ++value)
		toolBar->ToggleTool(BUTTON_ID_BASE + value, value == subtool);
	mode = static_cast<VisualToolCurvedTextMode>(subtool);
	MakeFeatures();
	parent->Render();
}

void VisualToolCurvedText::UpdateAlignmentTool() {
	if (!toolBar || !active_line)
		return;
	int effective = state.has_alignment ? state.alignment : ((GetLineAlignment(active_line) - 1) % 3 + 1);
	wxString source = state.has_alignment ? _("explicit") : _("from \\an");
	wxString help = wxString::Format(_("\\ctan%d (%s). Click to cycle start / center / end alignment."),
		effective, source.c_str());
	toolBar->SetToolShortHelp(BUTTON_ID_ALIGNMENT, help);
	toolBar->SetToolLongHelp(BUTTON_ID_ALIGNMENT, help);
}

void VisualToolCurvedText::CycleAlignment() {
	if (!active_line)
		return;
	int current = state.has_alignment ? state.alignment : ((GetLineAlignment(active_line) - 1) % 3 + 1);
	int next = current % 3 + 1;
	if (SetMangetsuCurvedTextAlignment(*active_line, next)) {
		state.alignment = next;
		state.has_alignment = true;
		Commit(_("curved text alignment"));
		UpdateAlignmentTool();
		parent->Render();
	}
}

void VisualToolCurvedText::DoRefresh() {
	features.clear();
	sel_features.clear();
	active_feature = nullptr;
	spline.clear();
	editable = false;
	provisional = false;
	if (!active_line)
		return;

	anchor = GetLinePositionAtFrame(active_line);
	state = GetMangetsuCurvedText(*active_line);
	std::string path = state.path;
	if (!state.has_path) {
		auto extents = GetLineBaseExtents(active_line);
		float half_width = std::max(25.f, (extents.second.X() - extents.first.X()) / 2.f);
		path = "m " + Vector2D(-half_width, 0).Str(' ') + " l " + Vector2D(half_width, 0).Str(' ');
		provisional = true;
	}
	if (!IsSupportedMangetsuCurvedTextPath(path)) {
		UpdateAlignmentTool();
		return;
	}
	spline.DecodeFromAss(path);
	editable = !spline.empty();
	MakeFeatures();
	UpdateAlignmentTool();
}

void VisualToolCurvedText::AddPathFeature(size_t index) {
	auto feature = agi::make_unique<Feature>();
	feature->curve = index;
	feature->role = CT_FEATURE_PATH;
	auto const& curve = spline[index];
	if (curve.type == SplineCurve::POINT) {
		feature->pos = LocalToScreen(curve.p1);
		feature->point = 0;
		feature->type = DRAG_SMALL_CIRCLE;
	}
	else if (curve.type == SplineCurve::LINE) {
		feature->pos = LocalToScreen(curve.p2);
		feature->point = 1;
		feature->type = DRAG_SMALL_CIRCLE;
	}
	else {
		feature->pos = LocalToScreen(curve.p2);
		feature->point = 1;
		feature->type = DRAG_SMALL_SQUARE;
		features.push_back(*feature.release());

		feature = agi::make_unique<Feature>();
		feature->curve = index;
		feature->role = CT_FEATURE_PATH;
		feature->pos = LocalToScreen(curve.p3);
		feature->point = 2;
		feature->type = DRAG_SMALL_SQUARE;
		features.push_back(*feature.release());

		feature = agi::make_unique<Feature>();
		feature->curve = index;
		feature->role = CT_FEATURE_PATH;
		feature->pos = LocalToScreen(curve.p4);
		feature->point = 3;
		feature->type = DRAG_SMALL_CIRCLE;
	}
	features.push_back(*feature.release());
}

void VisualToolCurvedText::MakeFeatures() {
	features.clear();
	sel_features.clear();
	active_feature = nullptr;
	if (!editable)
		return;
	if (mode == CT_EDIT_PATH) {
		for (size_t index = 0; index < spline.size(); ++index)
			AddPathFeature(index);
	}
	else if (mode == CT_ALONG_OFFSET || mode == CT_NORMAL_OFFSET) {
		auto feature = agi::make_unique<Feature>();
		Vector2D tangent;
		Vector2D path_point = PathPointAtDistance(static_cast<float>(state.along_offset), &tangent);
		if (mode == CT_ALONG_OFFSET) {
			feature->role = CT_FEATURE_ALONG;
			feature->pos = LocalToScreen(path_point);
			feature->type = DRAG_BIG_SQUARE;
		}
		else {
			feature->role = CT_FEATURE_NORMAL;
			feature->pos = LocalToScreen(path_point + tangent.Perpendicular().Unit() * static_cast<float>(state.normal_offset));
			feature->type = DRAG_BIG_CIRCLE;
		}
		features.push_back(*feature.release());
	}
}

void VisualToolCurvedText::Draw() {
	if (!active_line || !editable)
		return;
	wxColour line_color = to_wx(line_color_primary_opt->GetColor());
	wxColour secondary = to_wx(line_color_secondary_opt->GetColor());
	gl.SetLineColour(line_color, provisional ? .55f : .9f, 2);
	for (auto const& curve : spline) {
		if (curve.type == SplineCurve::POINT)
			continue;
		int steps = curve.type == SplineCurve::BICUBIC ? 48 : 1;
		std::vector<float> points;
		points.reserve((steps + 1) * 2);
		for (int step = 0; step <= steps; ++step) {
			auto screen = LocalToScreen(curve.GetPoint(step / static_cast<float>(steps)));
			points.push_back(screen.X());
			points.push_back(screen.Y());
		}
		gl.DrawLineStrip(2, points);
	}

	gl.SetLineColour(secondary, .85f, 1);
	for (auto const& curve : spline) {
		if (curve.type == SplineCurve::BICUBIC) {
			gl.DrawDashedLine(LocalToScreen(curve.p1), LocalToScreen(curve.p2), 6);
			gl.DrawDashedLine(LocalToScreen(curve.p3), LocalToScreen(curve.p4), 6);
		}
	}

	if (mode == CT_NORMAL_OFFSET && !features.empty()) {
		Vector2D tangent;
		Vector2D base = PathPointAtDistance(static_cast<float>(state.along_offset), &tangent);
		gl.DrawDashedLine(LocalToScreen(base), features.front().pos, 5);
	}
	if (mode == CT_EDIT_PATH && holding && drag_start && mouse_pos) {
		Vector2D top_left = drag_start.Min(mouse_pos);
		Vector2D bottom_right = drag_start.Max(mouse_pos);
		gl.DrawDashedLine(top_left, Vector2D(top_left.X(), bottom_right.Y()), 6);
		gl.DrawDashedLine(Vector2D(top_left.X(), bottom_right.Y()), bottom_right, 6);
		gl.DrawDashedLine(bottom_right, Vector2D(bottom_right.X(), top_left.Y()), 6);
		gl.DrawDashedLine(Vector2D(bottom_right.X(), top_left.Y()), top_left, 6);
	}
	DrawAllFeatures();
}

bool VisualToolCurvedText::InitializeDrag(Feature *feature) {
	return editable && feature;
}

void VisualToolCurvedText::UpdateDrag(Feature *feature) {
	if (!feature || !editable)
		return;
	if (feature->role == CT_FEATURE_PATH) {
		spline.MovePoint(spline.begin() + feature->curve, feature->point, ScreenToLocal(feature->pos));
		SavePath();
	}
	else if (feature->role == CT_FEATURE_ALONG) {
		if (!EnsurePathTag())
			return;
		state.along_offset = ClosestPathDistance(ScreenToLocal(feature->pos));
		state.has_along_offset = true;
		SetMangetsuCurvedTextAlongOffset(*active_line, state.along_offset);
		feature->pos = LocalToScreen(PathPointAtDistance(static_cast<float>(state.along_offset)));
	}
	else {
		if (!EnsurePathTag())
			return;
		Vector2D tangent;
		Vector2D base = PathPointAtDistance(static_cast<float>(state.along_offset), &tangent);
		Vector2D normal = tangent.Perpendicular().Unit();
		state.normal_offset = (ScreenToLocal(feature->pos) - base).Dot(normal);
		state.has_normal_offset = true;
		SetMangetsuCurvedTextNormalOffset(*active_line, state.normal_offset);
		feature->pos = LocalToScreen(base + normal * static_cast<float>(state.normal_offset));
	}
}

bool VisualToolCurvedText::InitializeHold() {
	if (!editable)
		return false;
	if (mode == CT_EDIT_PATH) {
		box_added.clear();
		return true;
	}
	if (mode == CT_MOVE_PATH && HitTestPath(mouse_pos)) {
		move_original_path = EncodePath();
		move_start_local = ScreenToLocal(drag_start);
		return true;
	}
	return false;
}

void VisualToolCurvedText::UpdateHold() {
	if (mode == CT_EDIT_PATH) {
		std::set<Feature *> boxed;
		Vector2D p1 = drag_start.Min(mouse_pos);
		Vector2D p2 = drag_start.Max(mouse_pos);
		for (auto& feature : features)
			if (InBox(p1, p2, feature.pos))
				boxed.insert(&feature);
		boost::set_difference(boxed, sel_features, std::inserter(box_added, box_added.end()));
		boost::copy(boxed, std::inserter(sel_features, sel_features.end()));
		std::vector<Feature *> remove;
		boost::set_difference(box_added, boxed, std::back_inserter(remove));
		for (auto feature : remove)
			sel_features.erase(feature);
		return;
	}
	if (mode != CT_MOVE_PATH)
		return;
	spline.DecodeFromAss(move_original_path);
	Vector2D delta = ScreenToLocal(mouse_pos) - move_start_local;
	for (auto& curve : spline) {
		curve.p1 = curve.p1 + delta;
		if (curve.type == SplineCurve::LINE)
			curve.p2 = curve.p2 + delta;
		else if (curve.type == SplineCurve::BICUBIC) {
			curve.p2 = curve.p2 + delta;
			curve.p3 = curve.p3 + delta;
			curve.p4 = curve.p4 + delta;
		}
	}
	SavePath();
}

bool VisualToolCurvedText::HitTestPath(Vector2D screen_point) const {
	Vector2D local = ScreenToLocal(screen_point);
	for (auto const& curve : spline) {
		if (curve.type == SplineCurve::POINT)
			continue;
		Vector2D closest = curve.GetClosestPoint(local);
		if ((LocalToScreen(closest) - screen_point).Len() <= std::max(7, feature_size + 3))
			return true;
	}
	return false;
}

float VisualToolCurvedText::ClosestPathDistance(Vector2D local_point) const {
	float best_square = std::numeric_limits<float>::infinity();
	float best_distance = 0.f;
	float traversed = 0.f;
	for (auto const& curve : spline) {
		if (curve.type == SplineCurve::POINT)
			continue;
		int steps = curve.type == SplineCurve::BICUBIC ? 80 : 1;
		Vector2D previous = curve.GetPoint(0.f);
		for (int step = 1; step <= steps; ++step) {
			Vector2D current = curve.GetPoint(step / static_cast<float>(steps));
			Vector2D segment = current - previous;
			float length = segment.Len();
			float t = length > 0.f ? std::max(0.f, std::min(1.f, (local_point - previous).Dot(segment) / segment.SquareLen())) : 0.f;
			Vector2D closest = previous + segment * t;
			float square = (closest - local_point).SquareLen();
			if (square < best_square) {
				best_square = square;
				best_distance = traversed + length * t;
			}
			traversed += length;
			previous = current;
		}
	}
	return best_distance;
}

Vector2D VisualToolCurvedText::PathPointAtDistance(float distance, Vector2D *tangent) const {
	Vector2D fallback(0, 0);
	Vector2D fallback_tangent(1, 0);
	float traversed = 0.f;
	for (auto const& curve : spline) {
		if (curve.type == SplineCurve::POINT) {
			fallback = curve.p1;
			continue;
		}
		int steps = curve.type == SplineCurve::BICUBIC ? 80 : 1;
		Vector2D previous = curve.GetPoint(0.f);
		for (int step = 1; step <= steps; ++step) {
			Vector2D current = curve.GetPoint(step / static_cast<float>(steps));
			Vector2D segment = current - previous;
			float length = segment.Len();
			fallback = current;
			if (length > 0.f)
				fallback_tangent = segment.Unit();
			if (distance <= traversed + length) {
				float part = length > 0.f ? std::max(0.f, (distance - traversed) / length) : 0.f;
				if (tangent)
					*tangent = fallback_tangent;
				return previous + segment * part;
			}
			traversed += length;
			previous = current;
		}
	}
	if (tangent)
		*tangent = fallback_tangent;
	return fallback;
}
