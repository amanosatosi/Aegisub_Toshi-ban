// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "visual_tool_curved_text.h"

#include "video_display.h"
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
#include <wx/menu.h>
#include <wx/toolbar.h>

namespace {
int const BUTTON_ID_BASE = 1500;
int const BUTTON_ID_RESET = 1520;
int const BUTTON_ID_REVERSE = 1521;
int const BUTTON_ID_REMOVE_CURVE = 1522;
int const BUTTON_ID_ALIGNMENT = 1523;
int const BUTTON_ID_ANCHOR_BASE = 1550;
int const BUTTON_ID_ANCHOR_LEGACY = 1560;

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
	simple_arc = GetMangetsuCurvedTextArc(path, arc);
	return true;
}

bool VisualToolCurvedText::SaveArc() {
	if (!active_line || !editable || !simple_arc)
		return false;
	auto path = FormatMangetsuCurvedTextArc(arc);
	if (!SetMangetsuCurvedTextPath(*active_line, path))
		return false;
	state.path = path;
	state.has_path = true;
	spline.DecodeFromAss(path);
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
	AddTool("video/tool/curved_text/arc", CT_ARC);
	AddTool("video/tool/curved_text/edit", CT_EDIT_PATH);
	AddTool("video/tool/curved_text/insert", CT_INSERT_PATH_POINT);
	AddTool("video/tool/curved_text/remove_point", CT_REMOVE_PATH_POINT);
	toolBar->AddSeparator();
	auto add_action = [&](char const *name, int id) {
		auto command = cmd::get(name);
		int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
		toolBar->AddTool(id, command->StrDisplay(c), command->Icon(icon_size), command->GetTooltip("Video"));
	};
	add_action("video/tool/curved_text/reset", BUTTON_ID_RESET);
	add_action("video/tool/curved_text/reverse", BUTTON_ID_REVERSE);
	add_action("video/tool/curved_text/remove", BUTTON_ID_REMOVE_CURVE);
	toolBar->AddSeparator();
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
	SetSubTool(mode);
}

void VisualToolCurvedText::OnSubTool(wxCommandEvent& event) {
	if (event.GetId() == BUTTON_ID_ALIGNMENT)
		ShowAlignmentMenu();
	else if (event.GetId() == BUTTON_ID_RESET)
		ResetStraight();
	else if (event.GetId() == BUTTON_ID_REVERSE)
		ReversePath();
	else if (event.GetId() == BUTTON_ID_REMOVE_CURVE)
		RemoveCurve();
	else
		SetSubTool(event.GetId() - BUTTON_ID_BASE);
}

void VisualToolCurvedText::SetSubTool(int subtool) {
	if (subtool == CT_CHOOSE_ALIGNMENT) {
		ShowAlignmentMenu();
		return;
	}
	if (subtool == CT_RESET_STRAIGHT) {
		ResetStraight();
		return;
	}
	if (subtool == CT_REVERSE_PATH) {
		ReversePath();
		return;
	}
	if (subtool == CT_REMOVE_CURVE) {
		RemoveCurve();
		return;
	}
	if (!toolBar || subtool < CT_ARC || subtool >= CT_MODE_LAST)
		return;
	if (subtool == CT_ARC && state.has_path && !simple_arc) {
		mode = CT_EDIT_PATH;
		UpdateModeTools();
		parent->Render();
		return;
	}
	mode = static_cast<VisualToolCurvedTextMode>(subtool);
	UpdateModeTools();
	MakeFeatures();
	parent->Render();
}

void VisualToolCurvedText::UpdateModeTools() {
	if (!toolBar)
		return;
	for (int value = CT_ARC; value < CT_MODE_LAST; ++value)
		toolBar->ToggleTool(BUTTON_ID_BASE + value, value == mode);
}

void VisualToolCurvedText::UpdateAlignmentTool() {
	if (!toolBar || !active_line)
		return;
	wxString help = state.has_alignment ?
		wxString::Format(_("\\ctan%d is active. Choose a curve anchor."), state.alignment) :
		_("Legacy curved baseline (no \\ctan). Choose a curve anchor.");
	toolBar->SetToolShortHelp(BUTTON_ID_ALIGNMENT, help);
	toolBar->SetToolLongHelp(BUTTON_ID_ALIGNMENT, help);
}

void VisualToolCurvedText::ShowAlignmentMenu() {
	if (!active_line || !toolBar)
		return;
	wxMenu menu;
	menu.AppendCheckItem(BUTTON_ID_ANCHOR_LEGACY,
		_("Legacy baseline (no \\ctan)"))->Check(!state.has_alignment);
	menu.AppendSeparator();
	wxString rows[] = {_("Bottom"), _("Middle"), _("Top")};
	wxString columns[] = {_("Start"), _("Center"), _("End")};
	for (int row = 2; row >= 0; --row) {
		for (int column = 0; column < 3; ++column) {
			int value = row * 3 + column + 1;
			wxString label = wxString::Format("%d  ", value) +
				rows[row] + " / " + columns[column];
			menu.AppendCheckItem(BUTTON_ID_ANCHOR_BASE + value, label)->Check(
				state.has_alignment && state.alignment == value);
		}
		if (row)
			menu.AppendSeparator();
	}
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
		if (!active_line)
			return;
		bool changed = event.GetId() == BUTTON_ID_ANCHOR_LEGACY ?
			RemoveMangetsuCurvedTextAlignment(*active_line) :
			SetMangetsuCurvedTextAlignment(*active_line,
				event.GetId() - BUTTON_ID_ANCHOR_BASE);
		if (changed) {
			state = GetMangetsuCurvedText(*active_line);
			Commit(_("curved text alignment"));
			commit_id = -1;
			UpdateAlignmentTool();
			parent->Render();
		}
	});
	toolBar->PopupMenu(&menu);
}

void VisualToolCurvedText::ResetStraight() {
	if (!active_line || !state.has_path)
		return;
	std::string path;
	if (!ResetMangetsuCurvedTextPathStraight(state.path, path) ||
		!SetMangetsuCurvedTextPath(*active_line, path))
		return;
	removed_line = nullptr;
	Commit(_("reset curved text path"));
	commit_id = -1;
	DoRefresh();
	parent->Render();
}

void VisualToolCurvedText::ReversePath() {
	if (!active_line || !state.has_path)
		return;
	std::string path;
	if (!ReverseMangetsuCurvedTextPath(state.path, path) ||
		!SetMangetsuCurvedTextPath(*active_line, path))
		return;
	removed_line = nullptr;
	Commit(_("reverse curved text path"));
	commit_id = -1;
	DoRefresh();
	parent->Render();
}

void VisualToolCurvedText::RemoveCurve() {
	if (!active_line || !RemoveMangetsuCurvedTextPath(*active_line))
		return;
	removed_line = active_line;
	state = MangetsuCurvedTextState();
	spline.clear();
	features.clear();
	sel_features.clear();
	active_feature = nullptr;
	editable = false;
	simple_arc = false;
	Commit(_("remove curved text path"));
	commit_id = -1;
	parent->Render();
}

void VisualToolCurvedText::DoRefresh() {
	features.clear();
	sel_features.clear();
	active_feature = nullptr;
	spline.clear();
	editable = false;
	simple_arc = false;
	if (!active_line)
		return;

	anchor = GetLinePositionAtFrame(active_line);
	state = GetMangetsuCurvedText(*active_line);
	if (!state.has_path) {
		if (active_line == removed_line) {
			UpdateAlignmentTool();
			return;
		}
		auto extents = GetLineBaseExtents(active_line);
		double width = extents.second.X() - extents.first.X();
		// GetLineBaseExtents deliberately reports unscaled text metrics. Curved
		// layout distances follow shaped advances, so account for legacy fscx
		// here; Mangetsu's top-level \scale scales both text and path together.
		Vector2D text_scale;
		GetLineScale(active_line, text_scale);
		width *= text_scale.X() / 100.0;
		int path_alignment = state.has_alignment ? state.alignment : GetLineAlignment(active_line);
		auto path = MakeDefaultMangetsuCurvedTextPath(width, path_alignment);
		if (!SetMangetsuCurvedTextPath(*active_line, path)) {
			UpdateAlignmentTool();
			return;
		}
		state.path = path;
		state.has_path = true;
		Commit(_("create curved text path"));
		commit_id = -1;
	}
	else
		removed_line = nullptr;

	if (!IsSupportedMangetsuCurvedTextPath(state.path)) {
		mode = CT_EDIT_PATH;
		UpdateModeTools();
		UpdateAlignmentTool();
		return;
	}
	spline.DecodeFromAss(state.path);
	editable = !spline.empty();
	simple_arc = GetMangetsuCurvedTextArc(state.path, arc);
	if (mode == CT_ARC && !simple_arc)
		mode = CT_EDIT_PATH;
	UpdateModeTools();
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

void VisualToolCurvedText::AddArcFeature(CurvedTextFeatureRole role, Vector2D position, DraggableFeatureType type) {
	auto feature = agi::make_unique<Feature>();
	feature->role = role;
	feature->pos = LocalToScreen(position);
	feature->type = type;
	feature->layer = role == CT_FEATURE_ARC_BEND ? 2 : 1;
	features.push_back(*feature.release());
}

void VisualToolCurvedText::MakeFeatures() {
	features.clear();
	sel_features.clear();
	active_feature = nullptr;
	if (!editable)
		return;
	if (mode == CT_ARC && simple_arc) {
		AddArcFeature(CT_FEATURE_ARC_START, arc.start, DRAG_SMALL_CIRCLE);
		AddArcFeature(CT_FEATURE_ARC_END, arc.end, DRAG_SMALL_CIRCLE);
		AddArcFeature(CT_FEATURE_ARC_BEND, arc.bend, DRAG_BIG_CIRCLE);
	}
	else if (mode == CT_EDIT_PATH || mode == CT_INSERT_PATH_POINT || mode == CT_REMOVE_PATH_POINT) {
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
	gl.SetLineColour(line_color, .9f, 2);
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
	if (mode == CT_EDIT_PATH || mode == CT_INSERT_PATH_POINT || mode == CT_REMOVE_PATH_POINT) {
		for (auto const& curve : spline) {
			if (curve.type == SplineCurve::BICUBIC) {
				gl.DrawDashedLine(LocalToScreen(curve.p1), LocalToScreen(curve.p2), 6);
				gl.DrawDashedLine(LocalToScreen(curve.p3), LocalToScreen(curve.p4), 6);
			}
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
	if (!editable || !feature)
		return false;
	if (mode == CT_REMOVE_PATH_POINT && feature->role == CT_FEATURE_PATH) {
		DeletePathFeature(feature);
		return false;
	}
	return true;
}

void VisualToolCurvedText::UpdateDrag(Feature *feature) {
	if (!feature || !editable)
		return;
	if (feature->role == CT_FEATURE_ARC_START || feature->role == CT_FEATURE_ARC_BEND ||
		feature->role == CT_FEATURE_ARC_END)
	{
		Vector2D position = ScreenToLocal(feature->pos);
		if (feature->role == CT_FEATURE_ARC_START)
			arc.start = position;
		else if (feature->role == CT_FEATURE_ARC_BEND)
			arc.bend = position;
		else
			arc.end = position;
		SaveArc();
	}
	else if (feature->role == CT_FEATURE_PATH) {
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
	if (mode == CT_INSERT_PATH_POINT) {
		Spline::iterator curve;
		float t = 0.f;
		if (!FindClosestCurve(ScreenToLocal(mouse_pos), curve, t))
			return false;
		auto split = curve->Split(t);
		*curve = split.first;
		spline.insert(std::next(curve), split.second);
		SavePath();
		MakeFeatures();
		Commit(_("insert curved text path point"));
		parent->Render();
		return false;
	}
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

void VisualToolCurvedText::DeletePathFeature(Feature *feature) {
	if (!feature || feature->curve >= spline.size())
		return;
	auto curve = spline.begin() + feature->curve;
	if (curve->type == SplineCurve::BICUBIC && (feature->point == 1 || feature->point == 2)) {
		curve->type = SplineCurve::LINE;
		curve->p2 = curve->p4;
	}
	else {
		if (spline.size() <= 2)
			return;
		auto next = std::next(curve);
		if (next != spline.end()) {
			if (curve->type == SplineCurve::POINT) {
				next->p1 = next->EndPoint();
				next->type = SplineCurve::POINT;
			}
			else
				next->p1 = curve->p1;
		}
		spline.erase(curve);
	}
	if (SavePath()) {
		MakeFeatures();
		Commit(_("remove curved text path point"));
		parent->Render();
	}
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

bool VisualToolCurvedText::FindClosestCurve(Vector2D local_point, Spline::iterator& best_curve, float& best_t) {
	best_curve = spline.end();
	best_t = 0.f;
	float best_distance = std::numeric_limits<float>::infinity();
	for (auto curve = spline.begin(); curve != spline.end(); ++curve) {
		if (curve->type == SplineCurve::POINT)
			continue;
		float t = curve->GetClosestParam(local_point);
		float distance = (curve->GetPoint(t) - local_point).SquareLen();
		if (distance < best_distance) {
			best_distance = distance;
			best_curve = curve;
			best_t = t;
		}
	}
	return best_curve != spline.end();
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
