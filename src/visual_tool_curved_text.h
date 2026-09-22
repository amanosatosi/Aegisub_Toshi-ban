// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "mangetsu_curved_text.h"
#include "spline.h"
#include "visual_feature.h"
#include "visual_tool.h"

#include <set>
#include <string>

class wxCommandEvent;
class wxToolBar;

enum VisualToolCurvedTextMode {
	CT_ARC = 0,
	CT_EDIT_PATH,
	CT_INSERT_PATH_POINT,
	CT_REMOVE_PATH_POINT,
	CT_MOVE_PATH,
	CT_ALONG_OFFSET,
	CT_NORMAL_OFFSET,
	CT_MODE_LAST,
	CT_CYCLE_ALIGNMENT = CT_MODE_LAST,
	CT_RESET_STRAIGHT,
	CT_REVERSE_PATH,
	CT_REMOVE_CURVE
};

enum CurvedTextFeatureRole {
	CT_FEATURE_PATH,
	CT_FEATURE_ARC_START,
	CT_FEATURE_ARC_BEND,
	CT_FEATURE_ARC_END,
	CT_FEATURE_ALONG,
	CT_FEATURE_NORMAL
};

struct VisualToolCurvedTextDraggableFeature final : public VisualDraggableFeature {
	size_t curve = 0;
	int point = 0;
	CurvedTextFeatureRole role = CT_FEATURE_PATH;
};

class VisualToolCurvedText final : public VisualTool<VisualToolCurvedTextDraggableFeature> {
	wxToolBar *toolBar = nullptr;
	VisualToolCurvedTextMode mode = CT_ARC;
	Spline spline;
	MangetsuCurvedTextState state;
	MangetsuCurvedTextArc arc;
	Vector2D anchor;
	bool editable = false;
	bool simple_arc = false;
	AssDialogue *removed_line = nullptr;
	int feature_size = 4;
	std::set<Feature *> box_added;
	std::string move_original_path;
	Vector2D move_start_local;

	Vector2D LocalToScreen(Vector2D point) const;
	Vector2D ScreenToLocal(Vector2D point) const;
	std::string EncodePath() const;
	bool EnsurePathTag();
	bool SavePath();
	bool SaveArc();
	bool HitTestPath(Vector2D screen_point) const;
	bool FindClosestCurve(Vector2D local_point, Spline::iterator& curve, float& t);
	float ClosestPathDistance(Vector2D local_point) const;
	Vector2D PathPointAtDistance(float distance, Vector2D *tangent = nullptr) const;
	void AddPathFeature(size_t curve);
	void AddArcFeature(CurvedTextFeatureRole role, Vector2D position, DraggableFeatureType type);
	void MakeFeatures();
	void UpdateAlignmentTool();
	void UpdateModeTools();
	void DeletePathFeature(Feature *feature);

	void DoRefresh() override;
	void OnFrameChanged() override { DoRefresh(); }
	void Draw() override;
	bool InitializeDrag(Feature *feature) override;
	void UpdateDrag(Feature *feature) override;
	bool InitializeHold() override;
	void UpdateHold() override;

	void AddTool(std::string const& command_name, int id);
	void OnSubTool(wxCommandEvent& event);

public:
	VisualToolCurvedText(VideoDisplay *parent, agi::Context *context);
	void OnAttached() override { DoRefresh(); }
	void SetToolbar(wxToolBar *toolBar) override;
	void SetSubTool(int subtool) override;
	int GetSubTool() override { return mode; }
	void CycleAlignment();
	void ResetStraight();
	void ReversePath();
	void RemoveCurve();
};
