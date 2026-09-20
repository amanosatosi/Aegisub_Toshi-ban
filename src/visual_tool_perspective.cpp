// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
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
//
// Aegisub Project http://www.aegisub.org/

/// @file visual_tool_perspective.cpp
/// @brief 3D perspective visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_perspective.h"

#include "command/command.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "perspective_geometry.h"
#include "selection_controller.h"
#include "vector3d.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_style.h"
#include "video_display.h"

#include <libaegisub/format.h>
#include <libaegisub/split.h>
#include <libaegisub/util.h>

#include <libaegisub/log.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <wx/colour.h>

static const float pi = 3.1415926536f;
static const float deg2rad = pi / 180.f;
static const float rad2deg = 180.f / pi;
static const float default_screen_z = 312.5;
static const char *ambient_plane_key = "_aegi_perspective_ambient_plane";

static const int BUTTON_ID_BASE = 1400;

enum VisualToolPerspectiveFeatureType {
	FEATURE_INNER = 0,
	FEATURE_OUTER = 1,
	FEATURE_CENTER = 2,
	FEATURE_ORG = 3,
};

inline float VisualToolPerspective::screenZ() const {
	return default_screen_z * script_res.Y() / layout_res.Y();
}

void VisualToolPerspective::AddTool(std::string command_name, int setting) {
	cmd::Command *command = cmd::get(command_name);
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolBar->AddTool(BUTTON_ID_BASE + setting, command->StrDisplay(c), command->Icon(icon_size), command->GetTooltip("Video"), wxITEM_CHECK);
}

VisualToolPerspective::VisualToolPerspective(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualToolPerspectiveDraggableFeature>(parent, context)
, optOuter(OPT_SET("Tool/Visual/Perspective/Outer"))
, optOuterLocked(OPT_SET("Tool/Visual/Perspective/Outer Locked"))
, optGrid(OPT_SET("Tool/Visual/Perspective/Grid"))
, optOrgMode(OPT_SET("Tool/Visual/Perspective/Org Mode"))
, optMode(OPT_SET("Tool/Visual/Perspective/Mode"))
{
	old_outer.resize(4);
	old_inner.resize(4);

	settings = 0;
	if (optOuter->GetBool()) settings |= PERSP_OUTER;
	if (optOuterLocked->GetBool()) settings |= PERSP_LOCK_OUTER;
	if (optGrid->GetBool()) settings |= PERSP_GRID;
	settings |= optOrgMode->GetInt();
	settings |= optMode->GetInt() == PERSP_MODE_ARCH1T3CHT ? PERSP_MODE_ARCH1T3CHT : PERSP_MODE_DISTORT;

	MakeFeatures();
}

void VisualToolPerspective::SetToolbar(wxToolBar *toolBar) {
	this->toolBar = toolBar;

	toolBar->AddSeparator();

	AddTool("video/tool/perspective/distort", PERSP_MODE_DISTORT);
	AddTool("video/tool/perspective/arch1t3cht", PERSP_MODE_ARCH1T3CHT);
	toolBar->AddSeparator();

	AddTool("video/tool/perspective/plane", PERSP_OUTER);
	AddTool("video/tool/perspective/lock_outer", PERSP_LOCK_OUTER);
	AddTool("video/tool/perspective/grid", PERSP_GRID);
	AddTool("video/tool/perspective/orgmode/center", PERSP_ORGMODE);

	SetSubTool(settings);

	toolBar->Realize();
	toolBar->Show(true);
	toolBar->Bind(wxEVT_TOOL, &VisualToolPerspective::OnSubTool, this);
}

void VisualToolPerspective::OnSubTool(wxCommandEvent &e) {
	int id = e.GetId() - BUTTON_ID_BASE;
	if (id == PERSP_MODE_DISTORT || id == PERSP_MODE_ARCH1T3CHT) {
		SetSubTool((GetSubTool() & ~PERSP_MODE) | id);
	} else if (id == PERSP_ORGMODE) {
		cmd::call("video/tool/perspective/orgmode/cycle", c);
	} else {
		SetSubTool(GetSubTool() ^ id);
	}
}

void VisualToolPerspective::SetSubTool(int subtool) {
	if (toolBar == nullptr) {
		throw agi::InternalError("Vector clip toolbar hasn't been set yet!");
	}
	for (int i = 1; i < PERSP_LAST; i <<= 1)
		toolBar->ToggleTool(BUTTON_ID_BASE + i, i & subtool);
	int mode = subtool & PERSP_MODE;
	if (mode != PERSP_MODE_DISTORT && mode != PERSP_MODE_ARCH1T3CHT) {
		mode = PERSP_MODE_DISTORT;
		subtool = (subtool & ~PERSP_MODE) | mode;
	}
	toolBar->ToggleTool(BUTTON_ID_BASE + PERSP_MODE_DISTORT, mode == PERSP_MODE_DISTORT);
	toolBar->ToggleTool(BUTTON_ID_BASE + PERSP_MODE_ARCH1T3CHT, mode == PERSP_MODE_ARCH1T3CHT);

	bool arch1t3cht = mode == PERSP_MODE_ARCH1T3CHT;
	toolBar->EnableTool(BUTTON_ID_BASE + PERSP_OUTER, arch1t3cht);
	toolBar->EnableTool(BUTTON_ID_BASE + PERSP_LOCK_OUTER, arch1t3cht && (subtool & PERSP_OUTER));
	toolBar->EnableTool(BUTTON_ID_BASE + PERSP_GRID, arch1t3cht);
	toolBar->EnableTool(BUTTON_ID_BASE + PERSP_ORGMODE, arch1t3cht);

	cmd::Command *orgmode;
	switch (subtool & PERSP_ORGMODE) {
		case PERSP_ORGMODE_CENTER:
			orgmode = cmd::get("video/tool/perspective/orgmode/center");
			break;
		case PERSP_ORGMODE_NOFAX:
			orgmode = cmd::get("video/tool/perspective/orgmode/nofax");
			break;
		case PERSP_ORGMODE_KEEP:
			orgmode = cmd::get("video/tool/perspective/orgmode/keep");
			break;
		default:
			throw agi::InternalError("Invalid perspective subtool");
	}
	wxString orgmodehelp = orgmode->StrDisplay(c) + wxString(". Click to cycle.\n") + orgmode->GetTooltip("Video");
	toolBar->SetToolShortHelp(BUTTON_ID_BASE + PERSP_ORGMODE, orgmodehelp);
	toolBar->SetToolLongHelp(BUTTON_ID_BASE + PERSP_ORGMODE, orgmodehelp);
	toolBar->SetToolNormalBitmap(BUTTON_ID_BASE + PERSP_ORGMODE, orgmode->Icon(OPT_GET("App/Toolbar Icon Size")->GetInt()));
	toolBar->ToggleTool(BUTTON_ID_BASE + PERSP_ORGMODE, false);

	settings = subtool;

	if (arch1t3cht) {
		optOuter->SetBool(settings & PERSP_OUTER);
		optOuterLocked->SetBool(settings & PERSP_LOCK_OUTER);
		optGrid->SetBool(settings & PERSP_GRID);
		optOrgMode->SetInt(GetOrgMode());
	}
	optMode->SetInt(settings & PERSP_MODE);

	MakeFeatures();
	parent->Render();
}

int VisualToolPerspective::GetSubTool() {
	return settings;
}

bool VisualToolPerspective::HasOuter() {
	return !IsDistortMode() && GetSubTool() & PERSP_OUTER;
}

bool VisualToolPerspective::OuterLocked() {
	return HasOuter() && (GetSubTool() & PERSP_LOCK_OUTER);
}

bool VisualToolPerspective::IsDistortMode() const {
	return (settings & PERSP_MODE) == PERSP_MODE_DISTORT;
}

int VisualToolPerspective::GetOrgMode() {
	return GetSubTool() & PERSP_ORGMODE;
}

bool VisualToolPerspective::HasOrgf() {
	return GetOrgMode() == PERSP_ORGMODE_KEEP;
}

std::vector<Vector2D> VisualToolPerspective::FeaturePositions(std::vector<Feature *> features) const {
	std::vector<Vector2D> result;
	for (size_t i = 0; i < 4; i++) {
		result.push_back(features[i]->pos);
	}
	return result;
}

void VisualToolPerspective::UpdateInner() {
	std::vector<Vector2D> uv = MakeRect(c1, c2);
	std::vector<Vector2D> quad = FeaturePositions(outer_corners);
	for (int i = 0; i < 4; i++)
		inner_corners[i]->pos = UVToXY(quad, uv[i]);
}

void VisualToolPerspective::UpdateOuter() {
	if (!HasOuter())
		return;
	std::vector<Vector2D> uv = MakeRect(-c1 / (c2 - c1), (1 - c1) / (c2 - c1));
	std::vector<Vector2D> quad = FeaturePositions(inner_corners);
	for (int i = 0; i < 4; i++)
		outer_corners[i]->pos = UVToXY(quad, uv[i]);
}

void VisualToolPerspective::MakeFeatures() {
	sel_features.clear();
	features.clear();
	active_feature = nullptr;

	inner_corners.clear();
	outer_corners.clear();
	centerf = nullptr;
	orgf = nullptr;
	if (IsDistortMode()) {
		centerf = new Feature(this, FEATURE_CENTER, 0);
		centerf->type = DRAG_BIG_TRIANGLE;
		centerf->layer = 1;
		features.push_back(*centerf);
		for (int i = 0; i < 4; ++i) {
			inner_corners.push_back(new Feature(this, FEATURE_INNER, i));
			inner_corners.back()->type = i == 0 ? DRAG_SMALL_SQUARE : DRAG_SMALL_CIRCLE;
			features.push_back(*inner_corners.back());
		}
		DoRefresh();
		return;
	}

	centerf = new Feature(this, FEATURE_CENTER, 0);
	centerf->type = DRAG_BIG_TRIANGLE;
	features.push_back(*centerf);

	if (HasOrgf()) {
		orgf = new Feature(this, FEATURE_ORG, 0);
		orgf->type = DRAG_BIG_TRIANGLE;
		features.push_back(*orgf);
	}

	for (int i = 0; i < 4; i++) {
		inner_corners.push_back(new Feature(this, FEATURE_INNER, i));
		inner_corners.back()->type = DRAG_SMALL_CIRCLE;
		features.push_back(*inner_corners.back());

		if (HasOuter()) {
			outer_corners.push_back(new Feature(this, FEATURE_OUTER, i));
			outer_corners.back()->type = DRAG_SMALL_CIRCLE;
			features.push_back(*outer_corners.back());
		}
	}

	DoRefresh();
}

void VisualToolPerspective::Draw() {
	if (!active_line) return;

	wxColour line_color = to_wx(line_color_primary_opt->GetColor());
	wxColour line_color_secondary = to_wx(line_color_secondary_opt->GetColor());

	// Draw Quad
	gl.SetLineColour(line_color);
	for (int i = 0; i < 4; i++) {
		if (HasOuter()) {
			gl.DrawDashedLine(outer_corners[i]->pos, outer_corners[(i + 1) % 4]->pos, 6);
			gl.DrawLine(inner_corners[i]->pos, inner_corners[(i + 1) % 4]->pos);
		} else {
			gl.DrawDashedLine(inner_corners[i]->pos, inner_corners[(i + 1) % 4]->pos, 6);
		}
	}

	DrawAllFeatures();

	if (!IsDistortMode() && GetSubTool() & PERSP_GRID) {
		// Draw Grid - Copied and modified from visual_tool_rotatexy.cpp

		// Number of lines on each side of each axis
		static const int radius = 15;
		// Total number of lines, including center axis line
		static const int line_count = radius * 2 + 1;
		// Distance between each line in pixels
		static const int spacing = 20;
		// Length of each grid line in pixels from axis to one end
		static const int half_line_length = spacing * (radius + 1);
		static const float fade_factor = 0.9f / radius;

		// Transform grid
		gl.SetOrigin(FromScriptCoords(org));
		gl.SetScale(100 * video_res / script_res);
		gl.SetRotation(angle_x, angle_y, angle_z, script_res.Y() / layout_res.Y());
		gl.SetScale(fsc);
		gl.SetShear(fax, fay);
		Vector2D glScale = (bbox.second.Y() - bbox.first.Y()) * Vector2D(1, 1) / spacing / 4;
		gl.SetScale(100 * glScale);

		// Draw grid
		gl.SetLineColour(line_color_secondary, 0.5f, 2);
		gl.SetModeLine();
		float r = line_color_secondary.Red() / 255.f;
		float g = line_color_secondary.Green() / 255.f;
		float b = line_color_secondary.Blue() / 255.f;

		std::vector<float> colors(line_count * 8 * 4);
		for (int i = 0; i < line_count * 8; ++i) {
			colors[i * 4 + 0] = r;
			colors[i * 4 + 1] = g;
			colors[i * 4 + 2] = b;
			colors[i * 4 + 3] = (i + 3) % 4 > 1 ? 0 : (1.f - abs(i / 8 - radius) * fade_factor);
		}

		std::vector<float> points(line_count * 8 * 2);
		for (int i = 0; i < line_count; ++i) {
			int pos = spacing * (i - radius);

			points[i * 16 + 0] = pos;
			points[i * 16 + 1] = half_line_length;

			points[i * 16 + 2] = pos;
			points[i * 16 + 3] = 0;

			points[i * 16 + 4] = pos;
			points[i * 16 + 5] = 0;

			points[i * 16 + 6] = pos;
			points[i * 16 + 7] = -half_line_length;

			points[i * 16 + 8] = half_line_length;
			points[i * 16 + 9] = pos;

			points[i * 16 + 10] = 0;
			points[i * 16 + 11] = pos;

			points[i * 16 + 12] = 0;
			points[i * 16 + 13] = pos;

			points[i * 16 + 14] = -half_line_length;
			points[i * 16 + 15] = pos;
		}

		Vector2D offset = (ToScriptCoords(QuadMidpoint(FeaturePositions(inner_corners))) - org) / glScale;
		for (int i = 0; i < line_count * 8; ++i) {
			points[i * 2 + 0] += offset.X();
			points[i * 2 + 1] += offset.Y();
		}

		gl.DrawLines(2, points, 4, colors);

		gl.ResetTransform();
	}
}

void VisualToolPerspective::OnDoubleClick() {
	std::vector<Feature *> active_features = (HasOuter() && !OuterLocked()) ? outer_corners : inner_corners;
	int maxi = -1;
	float mind = -1;
	for (size_t i = 0; i < active_features.size(); i++) {
		float d = (active_features[i]->pos - mouse_pos).Len();
		if (maxi == -1 || d < mind) {
			maxi = i;
			mind = d;
		}
	}
	active_features[maxi]->pos = mouse_pos;
	UpdateDrag(active_features[maxi]);
	Commit();
}

void VisualToolPerspective::OnMouseEvent(wxMouseEvent &event) {
	// Override this so we can find out which modifier keys were held
	shift_down = event.ShiftDown();
	ctrl_down = event.CmdDown();
	alt_down = event.AltDown();
	VisualTool<Feature>::OnMouseEvent(event);
	shift_down = false;
	ctrl_down = false;
	alt_down = false;
};

void VisualToolPerspective::UpdateDrag(Feature *feature) {
	if (IsDistortMode()) {
		if (feature == centerf)
			MoveDistortPosition(feature);
		else
			DistortToText(feature);
		return;
	}

	if (feature == centerf) {
		Vector2D oldCenter = QuadMidpoint(FeaturePositions(inner_corners));
		if (HasOuter() && !OuterLocked()) {
			std::vector<Vector2D> quad = FeaturePositions(outer_corners);
			Vector2D olduv = XYToUV(quad, oldCenter);
			Vector2D newuv = XYToUV(quad, centerf->pos);
			c1 = c1 + newuv - olduv;
			c2 = c2 + newuv - olduv;
			UpdateInner();
		} else {
			Vector2D diff = centerf->pos - oldCenter;
			for (int i = 0; i < 4; i++) {
				inner_corners[i]->pos = inner_corners[i]->pos + diff;
			}
			UpdateOuter();
		}
	} else if (HasOrgf() && feature == orgf) {
		org = ToScriptCoords(feature->pos);
	}

	std::vector<Feature *> changed_quad;
	std::vector<Vector2D> changed_quad_old;
	if (feature->group == FEATURE_INNER) {
		changed_quad = inner_corners;
		changed_quad_old = old_inner;
	} else if (HasOuter() && feature->group == FEATURE_OUTER) {
		changed_quad = outer_corners;
		changed_quad_old = old_outer;
	}

	if (!changed_quad.empty() && !ctrl_down) {
		// Validate: If the quad isn't convex, the intersection of the diagonals will not lie inside it.
		Vector2D diag1 = changed_quad[2]->pos - changed_quad[0]->pos;
		Vector2D diag2 = changed_quad[1]->pos - changed_quad[3]->pos;
		Vector2D b = changed_quad[3]->pos - changed_quad[0]->pos;
		float center_la1, center_la2;
		Solve2x2(diag1.X(), diag2.X(), diag1.Y(), diag2.Y(), b.X(), b.Y(), center_la1, center_la2);
		if (center_la1 < 0 || center_la1 > 1 || -center_la2 < 0 || -center_la2 > 1) {
			TextToPersp();
			return;
		}
	}

	int i = feature->index;

	if (ctrl_down && !changed_quad.empty()) {
		if (alt_down) {
			if (shift_down) {
				int bestsnap = -1;
				float mindist = -1;
				for (int j = 0; j < 4; j++) {
					float dist = (feature->pos - changed_quad_old[j]).SquareLen();
					if (bestsnap == -1 || dist < mindist) {
						bestsnap = j;
						mindist = dist;
					}
				}
				feature->pos = changed_quad_old[bestsnap];
			} else {
				Vector2D center = QuadMidpoint(changed_quad_old);
				Vector2D diff = feature->pos - center;
				Vector2D snapDirection1 = (changed_quad_old[0] - center).Unit();
				Vector2D snapDirection2 = (changed_quad_old[1] - center).Unit();
				Vector2D snap1 = diff.Dot(snapDirection1) * snapDirection1;
				Vector2D snap2 = diff.Dot(snapDirection2) * snapDirection2;
				diff = (snap1 - diff).SquareLen() <= (snap2 - diff).SquareLen() ? snap1 : snap2;
				feature->pos = center + diff;
			}
		}

		Vector2D relUV = XYToUV(changed_quad_old, feature->pos) - Vector2D(0.5, 0.5);

		for (int j = 0; j < 4; j++) {
			Vector2D flipi(i == 1 || i == 2 ? -1 : 1, i >= 2 ? -1 : 1);
			Vector2D flipj(j == 1 || j == 2 ? -1 : 1, j >= 2 ? -1 : 1);
			changed_quad[j]->pos = UVToXY(changed_quad_old, Vector2D(0.5, 0.5) + relUV * flipi * flipj);
		}

		if (HasOuter()) {
			if (feature->group == FEATURE_INNER) {
				if (!OuterLocked()) {
					c1 = XYToUV(FeaturePositions(outer_corners), inner_corners[0]->pos);
					c2 = XYToUV(FeaturePositions(outer_corners), inner_corners[2]->pos);
					UpdateInner();
				} else {
					UpdateOuter();
				}
			} else if (feature->group == FEATURE_OUTER) {
				if (OuterLocked()) {
					c1 = XYToUV(FeaturePositions(outer_corners), inner_corners[0]->pos);
					c2 = XYToUV(FeaturePositions(outer_corners), inner_corners[2]->pos);
					UpdateOuter();
				} else {
					UpdateInner();
				}
			}
		}
	} else if (!changed_quad.empty() && HasOuter()) {
		// Normally dragging one corner
		if (feature->group == FEATURE_INNER) {
			if (!OuterLocked()) {
				Vector2D newuv = XYToUV(FeaturePositions(outer_corners), feature->pos);
				c1 = Vector2D(i == 0 || i == 3 ? newuv.X() : c1.X(), i < 2 ? newuv.Y() : c1.Y());
				c2 = Vector2D(i == 0 || i == 3 ? c2.X() : newuv.X(), i < 2 ? c2.Y() : newuv.Y());
				UpdateInner();
			} else {
				UpdateOuter();
			}
		} else if (feature->group == FEATURE_OUTER) {
			if (OuterLocked()) {
				Vector2D d1 = -c1 / (c2 - c1);
				Vector2D d2 = (1 - c1) / (c2 - c1);
				Vector2D newuv = XYToUV(FeaturePositions(inner_corners), feature->pos);
				d1 = Vector2D(i == 0 || i == 3 ? newuv.X() : d1.X(), i < 2 ? newuv.Y() : d1.Y());
				d2 = Vector2D(i == 0 || i == 3 ? d2.X() : newuv.X(), i < 2 ? d2.Y() : newuv.Y());
				c1 = -d1 / (d2 - d1);
				c2 = (1 - d1) / (d2 - d1);
				UpdateOuter();
			} else {
				UpdateInner();
			}
		}
	}

	if (!InnerToText())
		TextToPersp();
	SetFeaturePositions();
}

void VisualToolPerspective::EndDrag(Feature *feature) {
	SaveFeaturePositions();
	if (!IsDistortMode())
		SaveOuterToLines();
}

bool VisualToolPerspective::InitializeDrag(Feature *feature) {
	if (!IsDistortMode() || feature != centerf)
		return true;

	distort_position_states.clear();
	distort_drag_origin = ToScriptCoords(centerf->pos);
	for (auto line : c->selectionController->GetSelectedSet()) {
		if (FilterLockedLines() && IsLockedLine(line))
			continue;
		DistortPositionState state;
		state.line = line;
		state.position = GetLinePosition(line);
		state.has_move = GetLineMove(line, state.move_start, state.move_end, state.t1, state.t2);
		distort_position_states.push_back(state);
	}
	return true;
}

void VisualToolPerspective::WrapSetOverride(AssDialogue* line, std::string const& tag, float value, int precision, float defaultval) {
	std::string format = agi::format("%%.%df", precision);
	std::string formatted = agi::format(format.c_str(), value);
	std::string default_formatted = agi::format(format.c_str(), defaultval);
	if (formatted == default_formatted || (defaultval == 0 && agi::format(format.c_str(), -value) == default_formatted))
		RemoveOverride(line, tag);
	else
		SetOverride(line, tag, formatted);
}

bool VisualToolPerspective::InnerToText() {
	Vector2D q0 = ToScriptCoords(inner_corners[0]->pos);
	Vector2D q1 = ToScriptCoords(inner_corners[1]->pos);
	Vector2D q2 = ToScriptCoords(inner_corners[2]->pos);
	Vector2D q3 = ToScriptCoords(inner_corners[3]->pos);

	// Find a parallelogram projecting to the quad. This is independent of translation.
	float z1, z3;
	Vector2D diag = q2 - q0;
	Vector2D side2 = q1 - q2;
	Vector2D side3 = q3 - q2;
	Solve2x2(side2.X(), side3.X(), side2.Y(), side3.Y(), -diag.X(), -diag.Y(), z1, z3);

	Vector2D midpoint = QuadMidpoint(std::vector<Vector2D>({q0, q1, q2, q3}));

	if (GetOrgMode() == PERSP_ORGMODE_CENTER) {
		org = midpoint;
	} else if (GetOrgMode() == PERSP_ORGMODE_NOFAX) {
		Vector2D v1 = q1 - q0;
		Vector2D v3 = q3 - q0;
		// Look for a translation after which the quad will unproject to a rectangle.
		// Specifically, look for a vector t such that this happens after moving q0 to t.
		// The set of such vectors is cut out by the equation a (x^2 + y^2) - b1 x - b2 y + c
		// with the following coefficients.
		float a = (1 - z1) * (1 - z3);
		Vector2D b = z1 * v1 + z3 * v3 - z1 * z3 * (v1 + v3);
		float c = z1 * z3 * v1.Dot(v3) + (z1 - 1) * (z3 - 1) * screenZ() * screenZ();

		// Our default value for t, which would put \org at the center of the quad.
		// We'll try to find a value for \org that's as close as possible to it.
		Vector2D t = q0 - midpoint;

		// Handle all the edge cases. These can actually come up in practice, like when
		// starting from text without any perspective.
		if (a == 0) {
			// If b = 0 we get a trivial or impossible equation, so just keep the previous \org.
			if (b.SquareLen() != 0) {
				// The equation cuts out a line. Find the point closest to the previous t.
				t = t + b * ((c - t.Dot(b)) / b.SquareLen());
			}
		} else {
			// The equation cuts out a circle.
			// Complete the square to find center and radius.
			Vector2D circleCenter = b / (2 * a);
			float sqradius = (b.SquareLen() / (4 * a) - c) / a;

			if (sqradius <= 0) {
				// This is actually very rare.
				org = circleCenter;
			} else {
				// Find the point on the circle closest to the current \org.
				float radius = sqrt(sqradius);
				Vector2D center2t = t - circleCenter;
				if (center2t.Len() == 0) {
					t = circleCenter + Vector2D(radius, 0);
				} else {
					t = circleCenter + center2t / center2t.Len() * radius;
				}
			}
		}

		org = q0 - t;
	}

	// Normalize to org
	q0 = q0 - org;
	q1 = q1 - org;
	q2 = q2 - org;
	q3 = q3 - org;

	Vector3D r0 = Vector3D(q0, screenZ());
	Vector3D r1 = z1 * Vector3D(q1, screenZ());
	Vector3D r2 = (z1 + z3 - 1) * Vector3D(q2, screenZ());
	Vector3D r3 = z3 * Vector3D(q3, screenZ());
	std::vector<Vector3D> r({r0, r1, r2, r3});

	// Find the z coordinate of the point projecting to the origin
	float orgla0, orgla1;
	Vector3D side0 = r1 - r0;
	Vector3D side1 = r3 - r0;
	Solve2x2(side0.X(), side1.X(), side0.Y(), side1.Y(), -r0.X(), -r0.Y(), orgla0, orgla1);
	float orgz = (r0 + orgla0 * side0 + orgla1 * side1).Z();

	// Normalize so the origin has z=screenZ, and move the screen plane to z=0
	for (int i = 0; i < 4; i++)
		r[i] = r[i] * screenZ() / orgz - Vector3D(0, 0, screenZ());

	// Find the rotations
	Vector3D n = (r[1] - r[0]).Cross(r[3] - r[0]);
	float roty = atan(n.X() / n.Z());
	if (n.Z() < 0)
		roty += pi;
	n = n.RotateY(roty);
	float rotx = atan(n.Y() / n.Z());

	// Rotate into the z=0 plane
	for (int i = 0; i < 4; i++)
		r[i] = r[i].RotateY(roty).RotateX(rotx);

	Vector3D ab = r[1] - r[0];
	float rotz = atan(ab.Y() / ab.X());
	if (ab.X() < 0)
		rotz += pi;

	// Rotate to make the top side be horizontal
	for (int i = 0; i < 4; i++)
		r[i] = r[i].RotateZ(-rotz);

	// We now have a horizontal parallelogram in the plane, so find the shear and the dimensions
	ab = r[1] - r[0];
	Vector3D ad = r[3] - r[0];
	float rawfax = ad.X() / ad.Y();

	float quadwidth = ab.Len();
	float quadheight = abs(ad.Y());
	float scalex = quadwidth / std::max(bbox.second.X() - bbox.first.X(), 1.0f);
	float scaley = quadheight / std::max(bbox.second.Y() - bbox.first.Y(), 1.0f);
	Vector2D scale = Vector2D(scalex, scaley);

	float shiftv = align <= 3 ? 1 : (align <= 6 ? 0.5 : 0);
	float shifth = align % 3 == 0 ? 1 : (align % 3 == 2 ? 0.5 : 0);
	pos = org + r[0].XY() - bbox.first * scale + Vector2D(quadwidth * shifth, quadheight * shiftv);
	angle_x = rotx * rad2deg;
	angle_y = -roty * rad2deg;
	angle_z = -rotz * rad2deg;
	Vector2D oldfsc = fsc;
	fsc = 100 * scale;
	fax = rawfax * scaley / scalex;
	fay = 0;

	bord = bord * fsc / oldfsc;
	shad = shad * fsc / oldfsc;

	// Give up if any of these numbers were invalid
	std::vector<float> allvalues({fax, fsc.X(), fsc.Y(), angle_z, angle_x, angle_y, bord.X(), bord.Y(), shad.X(), shad.Y(), org.X(), org.Y(), pos.X(), pos.Y()});
	for (float f : allvalues) {
		if (!isfinite(f)) return false;
	}

	for (auto line : c->selectionController->GetSelectedSet()) {
		if (FilterLockedLines() && IsLockedLine(line))
			continue;

		auto style = c->ass->GetStyle(line->Style);
		// Maybe just set the tags manually so the line doesn't need to be parsed again for every tag?
		WrapSetOverride(line, "\\fax", fax, 6);
		WrapSetOverride(line, "\\fay", 0, 6);
		WrapSetOverride(line, "\\fscx", fsc.X(), 2, style->scalex);
		WrapSetOverride(line, "\\fscy", fsc.Y(), 2, style->scaley);
		WrapSetOverride(line, "\\frz", angle_z, 4, style->angle);
		WrapSetOverride(line, "\\frx", angle_x, 4);
		WrapSetOverride(line, "\\fry", angle_y, 4);
		RemoveOverride(line, "\\bord");
		RemoveOverride(line, "\\shad");
		WrapSetOverride(line, "\\xbord", bord.X(), 2, style->outline_w);
		WrapSetOverride(line, "\\ybord", bord.Y(), 2, style->outline_w);
		WrapSetOverride(line, "\\xshad", shad.X(), 2, style->shadow_w);
		WrapSetOverride(line, "\\yshad", shad.Y(), 2, style->shadow_w);
		SetOverride(line, "\\org", org.PStr());
		SetOverride(line, "\\pos", pos.PStr());
	}
	return true;
}

void VisualToolPerspective::SaveFeaturePositions() {
	for (int i = 0; i < 4; i++) {
		old_inner[i] = inner_corners[i]->pos;
		if (HasOuter())
			old_outer[i] = outer_corners[i]->pos;
	}
}

void VisualToolPerspective::SaveOuterToLines() {
	if (HasOuter()) {
		std::string plane_descriptor;
		for (int i = 0; i < 4; i++) {
			Vector2D saved_corner = ToScriptCoords(outer_corners[i]->pos);
			if (!isfinite(saved_corner.X()) || !isfinite(saved_corner.Y()))
				return;
			plane_descriptor += agi::format("%.2f;%.2f", saved_corner.X(), saved_corner.Y());
			if (i < 3) plane_descriptor += "|";
		}
		uint32_t plane_extra = c->ass->AddExtradata(ambient_plane_key, plane_descriptor);

		for (auto line : c->selectionController->GetSelectedSet()) {
			if (FilterLockedLines() && IsLockedLine(line))
				continue;

			// Let's reinvent the wheel a bit since extradata tooling is nonexistent
			std::vector<uint32_t> extra = line->ExtradataIds.get();
			std::vector<ExtradataEntry> entries = c->ass->GetExtradata(extra);
			for (int i = entries.size() - 1; i >= 0; i--) {
				if (entries[i].key == ambient_plane_key)
					extra.erase(extra.begin() + i, extra.begin() + i + 1);
			}
			extra.push_back(plane_extra);
			line->ExtradataIds = extra;
		}
	}
}

void VisualToolPerspective::SetFeaturePositions() {
	if (IsDistortMode()) {
		if (centerf && inner_corners.size() == 4)
			centerf->pos = QuadMidpoint(FeaturePositions(inner_corners));
		return;
	}
	centerf->pos = QuadMidpoint(FeaturePositions(inner_corners));
	if (orgf != nullptr)
		orgf->pos = FromScriptCoords(org);
}

void VisualToolPerspective::TextToPersp() {
	if (!active_line) return;

	org = GetLineOrigin(active_line);
	pos = GetLinePosition(active_line);
	if (!org)
		org = pos;

	GetLineRotation(active_line, angle_x, angle_y, angle_z);
	GetLineShear(active_line, fax, fay);
	GetLineScale(active_line, fsc);
	GetLineOutline(active_line, bord);
	GetLineShadow(active_line, shad);

	align = GetLineAlignment(active_line);

	bbox = GetLineBaseExtents(active_line);
	float textwidth = std::max(bbox.second.X() - bbox.first.X(), 1.f);
	float textheight = std::max(bbox.second.Y() - bbox.first.Y(), 1.f);
	double shiftx = 0., shifty = 0.;

	switch ((align - 1) % 3) {
		case 1:
			shiftx = -textwidth / 2;
			break;
		case 2:
			shiftx = -textwidth;
			break;
		default:
			break;
	}
	switch ((align - 1) / 3) {
		case 0:
			shifty = -textheight;
			break;
		case 1:
			shifty = -textheight / 2;
			break;
		default:
			break;
	}

	std::vector<Vector2D> textrect = MakeRect(bbox.first, bbox.second);
	for (int i = 0; i < 4; i++) {
		Vector2D p = textrect[i];
		// Apply \fax and \fay
		p = Vector2D(p.X() + p.Y() * fax, p.X() * fay + p.Y());
		// Translate to alignment point
		p = p + Vector2D(shiftx, shifty);
		// Apply scaling
		p = Vector2D(p.X() * fsc.X() / 100., p.Y() * fsc.Y() / 100.);
		// Translate relative to origin
		p = p + pos - org;
		// Rotate ZXY
		Vector3D q(p);
		q = q.RotateZ(-angle_z * deg2rad);
		q = q.RotateX(-angle_x * deg2rad);
		q = q.RotateY(angle_y * deg2rad);
		// Project
		q = (screenZ() / (q.Z() + screenZ())) * q;
		// Move to origin
		Vector2D r = q.XY() + org;
		inner_corners[i]->pos = FromScriptCoords(r);
	}

	for (auto const& extra : c->ass->GetExtradata(active_line->ExtradataIds)) {
		if (extra.key == ambient_plane_key) {
			std::vector<std::string> fields;
			agi::Split(fields, extra.value, '|');
			if (fields.size() != 4)
				break;

			std::vector<Vector2D> saved_outer;
			for (int i = 0; i < 4; i++) {
				std::vector<std::string> ordinates;
				agi::Split(ordinates, fields[i], ';');
				if (ordinates.size() != 2)
					break;

				double x, y;
				if (!agi::util::try_parse(ordinates[0], &x)) break;
				if (!agi::util::try_parse(ordinates[1], &y)) break;

				saved_outer.emplace_back(x, y);
			}
			if (saved_outer.size() != 4) break;

			Vector2D d1 = XYToUV(saved_outer, ToScriptCoords(inner_corners[0]->pos));
			Vector2D d2 = XYToUV(saved_outer, ToScriptCoords(inner_corners[2]->pos));
			if (isfinite(d1.X()) && isfinite(d1.Y()) && isfinite(d2.X()) && isfinite(d2.Y())) {
				c1 = d1;
				c2 = d2;
			}
		}
	}

	UpdateOuter();
}

void VisualToolPerspective::TextToDistort() {
	if (!active_line)
		return;

	// Build the subtitle's undistorted on-video quad using the existing ASS
	// placement/shear/scale/rotation machinery, then use it as the normalized
	// coordinate frame for Mangetsu's four distortion pins.
	TextToPersp();
	auto event_quad = FeaturePositions(inner_corners);
	Vector2D move_delta = FromScriptCoords(GetLinePositionAtFrame(active_line)) - FromScriptCoords(pos);
	for (auto& point : event_quad)
		point = point + move_delta;
	auto unit_bbox = GetFirstDistortUnitExtents();
	Vector2D event_size = bbox.second - bbox.first;
	if (event_size.X() > 0 && event_size.Y() > 0) {
		Vector2D unit_begin = (unit_bbox.first - bbox.first) / event_size;
		Vector2D unit_end = (unit_bbox.second - bbox.first) / event_size;
		distort_base_quad.clear();
		for (auto const& point : MakeRect(unit_begin, unit_end))
			distort_base_quad.push_back(UVToXY(event_quad, point));
	}
	else
		distort_base_quad = std::move(event_quad);
	distort_state = GetMangetsuDistort(*active_line);
	auto distorted = DistortQuadToScreen(distort_base_quad, distort_state);
	for (size_t i = 0; i < inner_corners.size(); ++i)
		inner_corners[i]->pos = distorted[i];
}

std::pair<Vector2D, Vector2D> VisualToolPerspective::GetFirstDistortUnitExtents() {
	AssDialogue unit(*active_line);
	unit.Text = GetMangetsuDistortUnitText(*active_line);
	return GetLineBaseExtents(&unit);
}

bool VisualToolPerspective::DistortToText(Feature* feature) {
	if (!feature || feature->group != FEATURE_INNER || distort_base_quad.size() != 4)
		return false;

	Vector2D normalized = XYToUV(distort_base_quad, feature->pos);
	if (!SetMangetsuDistortCorner(distort_state, static_cast<size_t>(feature->index), normalized)) {
		TextToDistort();
		return false;
	}

	for (auto line : c->selectionController->GetSelectedSet()) {
		if (FilterLockedLines() && IsLockedLine(line))
			continue;
		SetMangetsuDistort(*line, distort_state, feature->index);
	}
	return true;
}

bool VisualToolPerspective::MoveDistortPosition(Feature* feature) {
	if (!feature || feature != centerf)
		return false;
	Vector2D delta = ToScriptCoords(feature->pos) - distort_drag_origin;
	for (auto const& state : distort_position_states) {
		if (state.has_move) {
			if (state.t1 > 0 || state.t2 > 0)
				SetOverride(state.line, "\\move", agi::format("(%s,%s,%d,%d)",
					(state.move_start + delta).Str(), (state.move_end + delta).Str(), state.t1, state.t2));
			else
				SetOverride(state.line, "\\move", agi::format("(%s,%s)",
					(state.move_start + delta).Str(), (state.move_end + delta).Str()));
		}
		else
			SetOverride(state.line, "\\pos", (state.position + delta).PStr());
	}
	TextToDistort();
	SetFeaturePositions();
	return true;
}

void VisualToolPerspective::DoRefresh() {
	if (IsDistortMode())
		TextToDistort();
	else
		TextToPersp();
	SetFeaturePositions();
	SaveFeaturePositions();
}

VisualToolPerspectiveDraggableFeature::VisualToolPerspectiveDraggableFeature(VisualToolPerspective *tool, int group, int index) : tool(tool), group(group), index(index) {}

void VisualToolPerspectiveDraggableFeature::UpdateDrag(Vector2D d, bool single_axis) {
	if (tool->IsDistortMode()) {
		VisualDraggableFeature::UpdateDrag(d, single_axis);
		return;
	}

	if (tool->ctrl_down && tool->alt_down)
		single_axis = false;   // This is handled manually later on

	if (single_axis && !(group == FEATURE_CENTER && !(tool->HasOuter() && !tool->OuterLocked()))) {
		// Snap to the axes *inside* of the quad's perspective plane.
		std::vector<Vector2D> quad = tool->old_inner;
		Vector2D posUV = XYToUV(quad, pos);
		Vector2D axis1 = UVToXY(quad, posUV + Vector2D(1, 0)) - pos;
		Vector2D axis2 = UVToXY(quad, posUV + Vector2D(0, 1)) - pos;

		// Normalize and project
		axis1 = axis1.Unit();
		axis2 = axis2.Unit();
		Vector2D snap1 = d.Dot(axis1) * axis1;
		Vector2D snap2 = d.Dot(axis2) * axis2;
		d = (snap1 - d).SquareLen() <= (snap2 - d).SquareLen() ? snap1 : snap2;
		single_axis = false;
	}
	VisualDraggableFeature::UpdateDrag(d, single_axis);
}
