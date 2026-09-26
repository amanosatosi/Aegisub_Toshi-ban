// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

/// @file dialog_mangetsu_gradient.cpp
/// @brief Dialog editor for Mangetsu true-gradient tags

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "dialogs.h"
#include "gradient_placement_session.h"
#include "include/aegisub/context.h"
#include "mangetsu_gradient_placement.h"
#include "mangetsu_gradient_scope.h"
#include "selection_controller.h"
#include "text_selection_controller.h"
#include "video_display.h"
#include "video_controller.h"
#include "visual_tool_gradient_placement.h"

#include <libaegisub/format.h>
#include <libaegisub/color.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <memory>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include <libaegisub/signal.h>

namespace {

// Keep the add-stop marker easy to tune without changing the interaction code.
constexpr int STOP_PLACEMENT_Y_SNAP_THRESHOLD = 40;
constexpr int STOP_PLACEMENT_MARKER_SIZE = 10;

static std::string trim_copy(std::string str) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_space));
	str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(), str.end());
	return str;
}

static bool ends_with_percent(std::string const& str) {
	return !str.empty() && str.back() == '%';
}

static double clamp_percent(double pos) {
	return std::max(0.0, std::min(100.0, pos));
}

static int clamp_alpha(int alpha) {
	return std::max(0, std::min(255, alpha));
}

static int matching_paren(std::string const& text, int open);

static wxColour to_wx_color(agi::Color const& color) {
	return wxColour(color.r, color.g, color.b);
}

static agi::Color lerp_color(agi::Color a, agi::Color b, double t) {
	t = std::max(0.0, std::min(1.0, t));
	return agi::Color(
		static_cast<unsigned char>(std::lround(a.r + (b.r - a.r) * t)),
		static_cast<unsigned char>(std::lround(a.g + (b.g - a.g) * t)),
		static_cast<unsigned char>(std::lround(a.b + (b.b - a.b) * t)),
		static_cast<unsigned char>(std::lround(a.a + (b.a - a.a) * t)));
}

enum class TargetGroup {
	Main,
	Border
};

enum class ChannelMode {
	Color,
	Alpha
};

struct GradientStop {
	double pos = 0.0;
	agi::Color color{0, 0, 0};
	int alpha = 0;
};

struct TagRef {
	std::string name;
	std::string alias;
	std::string status_name;
};

class DialogMangetsuGradient;

class GradientStopBar final : public wxPanel {
	DialogMangetsuGradient *dialog;
	bool dragging = false;
	bool add_placement = false;
	bool add_pressing = false;
	bool marker_visible = false;
	wxPoint marker_pos;

	void OnPaint(wxPaintEvent&);
	void OnMouse(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnMouseCaptureLost(wxMouseCaptureLostEvent&);
	wxPoint SnapMarkerY(wxPoint pos) const;
	bool InPlacementArea(wxPoint pos) const;
	void UpdateMarker(wxPoint pos, bool force = false);
	void RestoreCursor();

public:
	GradientStopBar(wxWindow *parent, DialogMangetsuGradient *dialog);
	void BeginAddPlacement();
	void CancelAddPlacement();
	bool IsAdding() const { return add_placement; }
};

class DialogMangetsuGradient final : public wxDialog {
	friend class GradientStopBar;

	agi::Context *context = nullptr;
	AssDialogue *active_line = nullptr;

	TargetGroup group = TargetGroup::Main;
	ChannelMode mode = ChannelMode::Color;
	int main_index = 1;
	int border_index = 1;
	int angle = 0;
	int selected_stop = 0;
	bool dirty = false;
	bool existing = false;
	bool updating_controls = false;
	bool preview_pending = false;
	bool lock_placement = false;
	bool placement_capture_active = false;
	bool committing = false;

	std::string original_text;
	std::string loaded_value;
	std::vector<GradientStop> stops;
	std::vector<int> border_layers{1};
	struct WorkingState {
		std::vector<GradientStop> stops;
		int angle = 0;
		int selected_stop = 0;
		bool existing = false;
		bool exists_in_original = false;
		bool modified = false;
		bool cleared = false;
		bool lock_placement = false;
		mangetsu::PlacementRect placement_rect;
		std::string loaded_value;
	};
	using TargetKey = std::tuple<int, int, int>; // group, target index, color/alpha
	std::map<TargetKey, WorkingState> working;
	std::map<int, std::pair<double, double>> outline_sizes;
	std::map<int, std::pair<double, double>> original_outline_sizes;
	std::set<int> changed_outline_sizes;
	std::set<int> new_outline_layers;
	std::vector<TargetKey> visible_targets;
	mangetsu::GradientScope edit_scope;
	double border_size_x = 0;
	double border_size_y = 0;
	mangetsu::PlacementRect placement_rect;
	std::shared_ptr<GradientPlacementSession> placement_session;
	agi::signal::Connection active_line_connection;
	agi::signal::Connection file_commit_connection;
	wxString placement_status;

	wxTimer preview_timer;
	wxSpinCtrl *angle_ctrl = nullptr;
	wxListBox *target_list = nullptr;
	wxButton *delete_outline_button = nullptr;
	wxStaticText *editing_label = nullptr;
	wxPanel *outline_size_panel = nullptr;
	wxSpinCtrlDouble *border_x_ctrl = nullptr;
	wxSpinCtrlDouble *border_y_ctrl = nullptr;
	wxToggleButton *color_button = nullptr;
	wxToggleButton *alpha_button = nullptr;
	wxToggleButton *lock_placement_button = nullptr;
	wxStaticText *status_label = nullptr;
	wxButton *add_button = nullptr;
	wxButton *remove_button = nullptr;
	GradientStopBar *stop_bar = nullptr;
	wxString add_stop_status;

	TagRef CurrentTag() const;
	TagRef PlacementTag() const;
	TagRef OutputTag() const;
	std::string CurrentGroupName() const;
	std::string CurrentTargetName() const;
	std::string CurrentModeName() const;
	std::string CurrentStatus() const;

	void BuildControls();
	void RefreshAvailableTargets();
	TargetKey CurrentKey() const;
	void SaveWorkingState();
	void LoadWorkingState();
	void SwitchTarget(TargetKey key);
	void OnTargetSelected(wxCommandEvent&);
	void OnNewOutline(wxCommandEvent&);
	void OnDeleteOutline(wxCommandEvent&);
	void OnBorderSizeChanged(wxCommandEvent&);
	std::string BuildWorkingText(bool include_provisional = false,
		mangetsu::GradientScope *result_scope = nullptr);
	std::string FormatStateValue(TargetKey key, WorkingState const& state) const;
	mangetsu::GradientTarget ScopeTarget(TargetKey key) const;
	std::string FallbackTag(mangetsu::GradientTarget target, AssStyle const *style = nullptr) const;
	void PreviewWorkingText();
	void RefreshControls();
	void RefreshLightControls();
	void ResetDefaultGradient();
	bool LoadTagValue(std::string const& value, bool placement = false);
	std::string FormatAttachedTagValue() const;
	std::string FormatTagValue() const;
	bool PlacementSupported() const;
	bool PlacementTransformUnsupported() const;
	bool PlacementTagActive() const;
	bool HasVideo() const;
	bool IsLockedLine() const;
	void RefreshPlacementControl();
	void StartPlacementSession();
	void EndPlacementSession(bool restore_tool = true);
	void OnPlacementAccepted(mangetsu::PlacementRect const& rect);
	void OnPlacementInvalid();
	void OnPlacementCancelled();
	void OnPlacementToolDeactivated();
	void OnActiveLineChanged(AssDialogue *line);
	void OnFileCommit(int type, AssDialogue const* line);
	void OnPlacementToggle(wxCommandEvent&);
	void UpdateDirtyState();
	void ClearCurrent();
	void ReverseStops();
	void StartAddStopPlacement();
	void CancelAddStopPlacement(bool show_status = true);
	void FinishAddStopPlacement(double pos);
	void AddStop(double pos);
	void RemoveSelectedStop();
	void FlatZone();
	void EditSelectedStop();
	GradientStop SampleAt(double pos) const;
	void SortStops();

	static std::vector<std::string> TokenizeTagValue(std::string const& value);
	static bool ParseAssColor(std::string const& text, agi::Color& color);
	static bool ParseAssAlpha(std::string const& text, int& alpha);
	static std::string FormatAssAlpha(int alpha);

	int HitTestStop(wxPoint pos) const;
	double PosFromMouse(wxPoint pos) const;
	wxRect BarRect() const;
	wxRect StartSwatchRect() const;
	wxRect EndSwatchRect() const;

	void OnAngleChanged(wxCommandEvent&);
	void OnQuickAngle(int new_angle);
	void OnMode(ChannelMode new_mode);
	void OnApply(wxCommandEvent&);
	void OnOK(wxCommandEvent&);
	void OnCancel(wxCommandEvent&);
	void MarkGradientChanged(bool immediate = false, wxString const& message = wxString());
	void SchedulePreview(wxString const& message, bool immediate);
	void FlushPreview();
	void OnPreviewTimer(wxTimerEvent&);
	void OnDialogKeyDown(wxKeyEvent& event);
	void FinishDialog(int result);

public:
	DialogMangetsuGradient(wxWindow *parent, agi::Context *context);
	~DialogMangetsuGradient();
};

GradientStopBar::GradientStopBar(wxWindow *parent, DialogMangetsuGradient *dialog)
: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(660, 120), wxBORDER_NONE)
, dialog(dialog)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(560, 110));
	SetFocus();
	Bind(wxEVT_PAINT, &GradientStopBar::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_LEFT_DCLICK, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_LEFT_UP, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_MOTION, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_RIGHT_DOWN, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_CHAR_HOOK, &GradientStopBar::OnKeyDown, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &GradientStopBar::OnMouseCaptureLost, this);
}

void GradientStopBar::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetBackgroundColour()));
	dc.Clear();

	wxRect start = dialog->StartSwatchRect();
	wxRect end = dialog->EndSwatchRect();
	wxRect bar = dialog->BarRect();

	auto draw_checker = [&](wxRect rect) {
		int cell = 6;
		for (int y = rect.GetTop(); y <= rect.GetBottom(); y += cell) {
			for (int x = rect.GetLeft(); x <= rect.GetRight(); x += cell) {
				bool light = ((x / cell) + (y / cell)) % 2 == 0;
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(light ? wxColour(230, 230, 230) : wxColour(170, 170, 170)));
				dc.DrawRectangle(x, y, cell, cell);
			}
		}
	};

	auto stop_colour = [&](GradientStop const& stop) {
		if (dialog->mode == ChannelMode::Color)
			return to_wx_color(stop.color);
		int v = 255 - clamp_alpha(stop.alpha);
		return wxColour(v, v, v);
	};

	if (dialog->mode == ChannelMode::Alpha)
		draw_checker(bar);
	for (int x = 0; x < bar.GetWidth(); ++x) {
		double pos = bar.GetWidth() <= 1 ? 0.0 : 100.0 * x / (bar.GetWidth() - 1);
		dc.SetPen(wxPen(stop_colour(dialog->SampleAt(pos))));
		dc.DrawLine(bar.GetX() + x, bar.GetY(), bar.GetX() + x, bar.GetBottom());
	}
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
	dc.DrawRectangle(bar);

	auto draw_swatch = [&](wxRect rect, GradientStop const& stop, bool selected) {
		if (dialog->mode == ChannelMode::Alpha)
			draw_checker(rect);
		dc.SetBrush(wxBrush(stop_colour(stop)));
		dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
		dc.DrawRectangle(rect);
		if (selected) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(0, 120, 215), 3));
			dc.DrawRectangle(rect.Deflate(2, 2));
		}
	};

	if (!dialog->stops.empty()) {
		draw_swatch(start, dialog->stops.front(), dialog->selected_stop == 0);
		draw_swatch(end, dialog->stops.back(), dialog->selected_stop == static_cast<int>(dialog->stops.size()) - 1);
	}

	for (size_t i = 0; i < dialog->stops.size(); ++i) {
		double t = dialog->stops[i].pos / 100.0;
		int x = bar.GetX() + static_cast<int>(std::lround(t * bar.GetWidth()));
		wxRect swatch(x - 13, bar.GetY() - 34, 26, 24);
		dc.SetPen(wxPen(wxColour(0, 0, 0), 2));
		dc.DrawLine(x, bar.GetY() - 8, x, bar.GetBottom() + 8);
		dc.SetBrush(wxBrush(stop_colour(dialog->stops[i])));
		dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
		dc.DrawRectangle(swatch);
		if (static_cast<int>(i) == dialog->selected_stop) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(0, 120, 215), 3));
			dc.DrawRectangle(swatch.Deflate(1, 1));
		}
	}

	if (marker_visible && (add_placement || dragging)) {
		// Draw an under-stroke so the small blue placement tip remains legible
		// over both light and dark gradient colours.
		wxRect marker(marker_pos.x - STOP_PLACEMENT_MARKER_SIZE / 2,
			marker_pos.y - STOP_PLACEMENT_MARKER_SIZE / 2,
			STOP_PLACEMENT_MARKER_SIZE, STOP_PLACEMENT_MARKER_SIZE);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(wxColour(15, 35, 55), 4));
		dc.DrawRectangle(marker);
		dc.SetPen(wxPen(wxColour(55, 155, 255), 2));
		dc.DrawRectangle(marker);

		if (add_placement) {
			dc.SetPen(wxPen(wxColour(55, 155, 255), 1, wxPENSTYLE_DOT));
			dc.DrawLine(marker_pos.x, bar.GetTop(), marker_pos.x, bar.GetBottom());
			wxString percent = wxString::Format("%.1f%%", dialog->PosFromMouse(marker_pos));
			dc.SetTextForeground(wxColour(35, 125, 220));
			dc.DrawText(percent, marker_pos.x + 8, std::max(0, marker_pos.y - 18));
		}
	}
}

void GradientStopBar::OnMouse(wxMouseEvent& event) {
	SetFocus();
	wxPoint pos = event.GetPosition();

	if (event.Leaving() && !add_pressing && !dragging) {
		marker_visible = false;
		RestoreCursor();
		Refresh(false);
		return;
	}

	if (add_placement) {
		if (event.RightDown()) {
			dialog->CancelAddStopPlacement();
			return;
		}

		if (event.Moving() || event.Dragging() || event.Entering())
			UpdateMarker(pos, add_pressing);

		if (event.LeftDown()) {
			if (!InPlacementArea(pos)) {
				dialog->add_stop_status = _("Move over the gradient strip to place the new stop.");
				dialog->RefreshLightControls();
				return;
			}
			add_pressing = true;
			UpdateMarker(pos, true);
			if (!HasCapture())
				CaptureMouse();
			return;
		}

		if (event.LeftUp() && add_pressing) {
			add_pressing = false;
			UpdateMarker(pos, true);
			if (HasCapture())
				ReleaseMouse();
			dialog->FinishAddStopPlacement(dialog->PosFromMouse(pos));
			return;
		}
		return;
	}

	int hit = dialog->HitTestStop(pos);

	if (event.RightDown() && hit >= 0) {
		dialog->selected_stop = hit;
		dialog->RefreshLightControls();
		return;
	}

	if (event.LeftDClick() && hit >= 0) {
		dialog->selected_stop = hit;
		dialog->EditSelectedStop();
		return;
	}

	if (event.LeftDown()) {
		if (hit >= 0) {
			dialog->selected_stop = hit;
			dragging = dialog->selected_stop > 0 && dialog->selected_stop < static_cast<int>(dialog->stops.size()) - 1;
			if (dragging)
				CaptureMouse();
			dialog->RefreshLightControls();
			return;
		}
		if (event.CmdDown() || event.ControlDown()) {
			dialog->AddStop(dialog->PosFromMouse(pos));
			return;
		}
	}

	if (event.Dragging() && event.LeftIsDown() && dragging) {
		UpdateMarker(pos, true);
		dialog->stops[dialog->selected_stop].pos = dialog->PosFromMouse(pos);
		dialog->SortStops();
		dialog->MarkGradientChanged(false);
		return;
	}

	if (event.LeftUp() && dragging) {
		dragging = false;
		marker_visible = false;
		if (HasCapture())
			ReleaseMouse();
		dialog->FlushPreview();
		dialog->RefreshLightControls();
	}
}

void GradientStopBar::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && add_placement) {
		dialog->CancelAddStopPlacement();
		return;
	}
	if (event.GetKeyCode() == WXK_DELETE) {
		dialog->RemoveSelectedStop();
		return;
	}
	event.Skip();
}

void GradientStopBar::OnMouseCaptureLost(wxMouseCaptureLostEvent&) {
	dragging = false;
	add_pressing = false;
	marker_visible = false;
	RestoreCursor();
	if (add_placement)
		dialog->CancelAddStopPlacement();
	else
		Refresh(false);
}

wxPoint GradientStopBar::SnapMarkerY(wxPoint pos) const {
	wxRect const bar = dialog->BarRect();
	int const top_distance = std::abs(pos.y - bar.GetTop());
	int const bottom_distance = std::abs(pos.y - bar.GetBottom());
	if (std::min(top_distance, bottom_distance) <= STOP_PLACEMENT_Y_SNAP_THRESHOLD)
		pos.y = top_distance <= bottom_distance ? bar.GetTop() : bar.GetBottom();
	return pos;
}

bool GradientStopBar::InPlacementArea(wxPoint pos) const {
	wxRect area = dialog->BarRect();
	area.Inflate(0, STOP_PLACEMENT_Y_SNAP_THRESHOLD);
	return area.Contains(pos);
}

void GradientStopBar::UpdateMarker(wxPoint pos, bool force) {
	marker_visible = force || InPlacementArea(pos);
	if (marker_visible) {
		marker_pos = SnapMarkerY(pos); // Deliberately leaves X completely untouched.
		if (add_placement)
			SetCursor(wxCursor(wxCURSOR_BLANK));
	}
	else
		RestoreCursor();
	Refresh(false);
}

void GradientStopBar::RestoreCursor() {
	SetCursor(wxNullCursor);
}

void GradientStopBar::BeginAddPlacement() {
	if (add_placement)
		return;
	add_placement = true;
	add_pressing = false;
	marker_visible = false;
	SetFocus();
	Refresh(false);
}

void GradientStopBar::CancelAddPlacement() {
	add_placement = false;
	add_pressing = false;
	marker_visible = false;
	if (HasCapture())
		ReleaseMouse();
	RestoreCursor();
	Refresh(false);
}

DialogMangetsuGradient::DialogMangetsuGradient(wxWindow *parent, agi::Context *context)
: wxDialog(parent, wxID_ANY, _("Mangetsu Gradient Editor"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, context(context)
{
	active_line = context && context->selectionController ? context->selectionController->GetActiveLine() : nullptr;
	if (active_line)
		original_text = active_line->Text.get();
	if (context && context->textSelectionController)
		edit_scope = mangetsu::ResolveGradientScope(original_text,
			context->textSelectionController->GetSelectionStart(),
			context->textSelectionController->GetSelectionEnd());
	else
		edit_scope = mangetsu::ResolveGradientScope(original_text, 0, 0);

	ResetDefaultGradient();
	BuildControls();
	RefreshAvailableTargets();
	LoadWorkingState();
	RefreshControls();
	PreviewWorkingText();
	if (context && context->selectionController)
		active_line_connection = context->selectionController->AddActiveLineListener(&DialogMangetsuGradient::OnActiveLineChanged, this);
	if (context && context->ass)
		file_commit_connection = context->ass->AddCommitListener(&DialogMangetsuGradient::OnFileCommit, this);
	if (lock_placement && placement_rect.valid)
		StartPlacementSession();
}

DialogMangetsuGradient::~DialogMangetsuGradient() {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	EndPlacementSession();
}

void DialogMangetsuGradient::BuildControls() {
	auto *root = new wxBoxSizer(wxVERTICAL);
	auto *body = new wxBoxSizer(wxHORIZONTAL);
	auto *editor = new wxBoxSizer(wxVERTICAL);
	auto *target_box = new wxStaticBoxSizer(wxVERTICAL, this, _("Target"));
	target_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(145, 230));
	target_box->Add(target_list, 1, wxEXPAND | wxALL, 4);
	auto *new_outline = new wxButton(this, wxID_ANY, _("+ New outline"));
	target_box->Add(new_outline, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	delete_outline_button = new wxButton(this, wxID_ANY, _("Delete outline"));
	target_box->Add(delete_outline_button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	body->Add(target_box, 0, wxEXPAND | wxALL, 8);
	editing_label = new wxStaticText(this, wxID_ANY, _("Editing: Primary"));
	{
		wxFont font = editing_label->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		editing_label->SetFont(font);
	}
	editor->Add(editing_label, 0, wxLEFT | wxRIGHT | wxTOP, 8);
	auto *top = new wxBoxSizer(wxHORIZONTAL);

	auto *angle_label = new wxStaticText(this, wxID_ANY, _("angle"));
	angle_ctrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 359, 0);
	top->Add(angle_label, wxSizerFlags().Center().Border(wxRIGHT, 4));
	top->Add(angle_ctrl, wxSizerFlags().Center().Border(wxRIGHT, 8));

	struct QuickAngle {
		const char *label;
		int angle;
	};
	static const QuickAngle quick_angles[] = {
		{"\xE2\x86\x92", 0},
		{"\xE2\x86\x93", 90},
		{"\xE2\x86\x91", 270},
		{"\xE2\x86\x96", 225},
		{"\xE2\x86\x97", 315},
		{"\xE2\x86\x99", 135},
		{"\xE2\x86\x98", 45}
	};

	for (auto const& quick : quick_angles) {
		auto *button = new wxButton(this, wxID_ANY, wxString::FromUTF8(quick.label), wxDefaultPosition, wxSize(34, -1));
		button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { OnQuickAngle(quick.angle); });
		top->Add(button, wxSizerFlags().Center().Border(wxRIGHT, 2));
	}

	lock_placement_button = new wxToggleButton(this, wxID_ANY, _("Lock Placement"));
	lock_placement_button->SetToolTip(_("Lock the gradient to a fixed area of the video.\nEnable this, then drag the area in the video preview.\nOutside the area, Mangetsu uses the line's normal primary color."));
	top->Add(lock_placement_button, wxSizerFlags().Center().Border(wxRIGHT, 8));

	top->AddSpacer(12);

	color_button = new wxToggleButton(this, wxID_ANY, _("Color"));
	alpha_button = new wxToggleButton(this, wxID_ANY, _("Alpha"));
	top->Add(color_button, wxSizerFlags().Center().Border(wxRIGHT, 2));
	top->Add(alpha_button, wxSizerFlags().Center());

	stop_bar = new GradientStopBar(this, this);
	status_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	outline_size_panel = new wxPanel(this);
	auto *size_sizer = new wxBoxSizer(wxHORIZONTAL);
	size_sizer->Add(new wxStaticText(outline_size_panel, wxID_ANY, _("Border size  X:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	border_x_ctrl = new wxSpinCtrlDouble(outline_size_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
		wxSize(85, -1), wxSP_ARROW_KEYS, 0, 1000, 2, 0.25);
	border_x_ctrl->SetDigits(2);
	size_sizer->Add(border_x_ctrl, 0, wxRIGHT, 10);
	size_sizer->Add(new wxStaticText(outline_size_panel, wxID_ANY, _("Y:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	border_y_ctrl = new wxSpinCtrlDouble(outline_size_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
		wxSize(85, -1), wxSP_ARROW_KEYS, 0, 1000, 2, 0.25);
	border_y_ctrl->SetDigits(2);
	size_sizer->Add(border_y_ctrl);
	outline_size_panel->SetSizer(size_sizer);

	auto *ops = new wxBoxSizer(wxHORIZONTAL);
	add_button = new wxButton(this, wxID_ANY, _("+ Stop"));
	add_button->SetToolTip(_("Choose a position for a new stop on the gradient strip."));
	remove_button = new wxButton(this, wxID_ANY, _("Remove"));
	auto *reverse_button = new wxButton(this, wxID_ANY, _("Reverse"));
	auto *flat_button = new wxButton(this, wxID_ANY, _("Flat Zone"));
	auto *clear_button = new wxButton(this, wxID_ANY, _("Clear"));
	ops->Add(add_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(remove_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(reverse_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(flat_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(clear_button, wxSizerFlags().Border(wxRIGHT, 4));

	auto *buttons = new wxBoxSizer(wxHORIZONTAL);
	auto *ok = new wxButton(this, wxID_OK, _("OK"));
	auto *apply = new wxButton(this, wxID_APPLY, _("Apply"));
	auto *cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddStretchSpacer(1);
	buttons->Add(ok, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(apply, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(cancel);

	editor->Add(top, wxSizerFlags().Expand().Border(wxALL, 8));
	editor->Add(outline_size_panel, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	editor->Add(stop_bar, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	editor->Add(status_label, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	editor->Add(ops, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	body->Add(editor, 1, wxEXPAND);
	root->Add(body, 1, wxEXPAND);
	root->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	SetSizerAndFit(root);
	SetMinSize(GetSize());

	angle_ctrl->Bind(wxEVT_SPINCTRL, &DialogMangetsuGradient::OnAngleChanged, this);
	angle_ctrl->Bind(wxEVT_TEXT, &DialogMangetsuGradient::OnAngleChanged, this);
	target_list->Bind(wxEVT_LISTBOX, &DialogMangetsuGradient::OnTargetSelected, this);
	new_outline->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnNewOutline, this);
	delete_outline_button->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnDeleteOutline, this);
	border_x_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) {
		wxCommandEvent event; OnBorderSizeChanged(event);
	});
	border_y_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) {
		wxCommandEvent event; OnBorderSizeChanged(event);
	});
	border_x_ctrl->Bind(wxEVT_TEXT, &DialogMangetsuGradient::OnBorderSizeChanged, this);
	border_y_ctrl->Bind(wxEVT_TEXT, &DialogMangetsuGradient::OnBorderSizeChanged, this);
	color_button->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent&) { OnMode(ChannelMode::Color); });
	alpha_button->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent&) { OnMode(ChannelMode::Alpha); });
	lock_placement_button->Bind(wxEVT_TOGGLEBUTTON, &DialogMangetsuGradient::OnPlacementToggle, this);
	add_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { StartAddStopPlacement(); });
	remove_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		RemoveSelectedStop();
	});
	reverse_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		ReverseStops();
	});
	flat_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		FlatZone();
	});
	clear_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		ClearCurrent();
	});
	ok->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnOK, this);
	apply->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnApply, this);
	cancel->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnCancel, this);
	preview_timer.Bind(wxEVT_TIMER, &DialogMangetsuGradient::OnPreviewTimer, this);
	Bind(wxEVT_CHAR_HOOK, &DialogMangetsuGradient::OnDialogKeyDown, this);
	Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent&) {
		wxCommandEvent evt;
		OnCancel(evt);
	});
}

void DialogMangetsuGradient::RefreshAvailableTargets() {
	border_layers = mangetsu::FindOutlineLayers(original_text);
	for (int layer : new_outline_layers)
		if (std::find(border_layers.begin(), border_layers.end(), layer) == border_layers.end())
			border_layers.push_back(layer);
	std::sort(border_layers.begin(), border_layers.end());
}

DialogMangetsuGradient::TargetKey DialogMangetsuGradient::CurrentKey() const {
	return {group == TargetGroup::Border ? 1 : 0,
		group == TargetGroup::Border ? border_index : main_index,
		mode == ChannelMode::Alpha ? 1 : 0};
}

mangetsu::GradientTarget DialogMangetsuGradient::ScopeTarget(TargetKey key) const {
	mangetsu::GradientTarget target;
	target.alpha = std::get<2>(key) != 0;
	if (std::get<0>(key)) {
		target.kind = mangetsu::GradientTargetKind::Outline;
		target.layer = std::get<1>(key);
	}
	else {
		switch (std::get<1>(key)) {
			case 2: target.kind = mangetsu::GradientTargetKind::Secondary; break;
			case 4: target.kind = mangetsu::GradientTargetKind::Shadow; break;
			case 5: target.kind = mangetsu::GradientTargetKind::Fifth; break;
			default: target.kind = mangetsu::GradientTargetKind::Primary; break;
		}
	}
	return target;
}

std::string DialogMangetsuGradient::FallbackTag(mangetsu::GradientTarget target, AssStyle const *style) const {
	if (!style && context && context->ass && active_line)
		style = context->ass->GetStyle(active_line->Style);
	agi::Color color = style ? style->primary : agi::Color(255, 255, 255);
	std::string tag = "\\1c";
	switch (target.kind) {
		case mangetsu::GradientTargetKind::Secondary:
			color = style ? style->secondary : agi::Color(255, 0, 0); tag = "\\2c"; break;
		case mangetsu::GradientTargetKind::Outline:
			color = style ? style->outline : agi::Color(0, 0, 0);
			tag = "\\" + std::to_string(target.layer) + "bc"; break;
		case mangetsu::GradientTargetKind::Shadow:
			color = style ? style->shadow : agi::Color(0, 0, 0); tag = "\\4c"; break;
		case mangetsu::GradientTargetKind::Fifth: tag = "\\5c"; break;
		default: break;
	}
	if (target.alpha) {
		tag = target.kind == mangetsu::GradientTargetKind::Outline ?
			"\\" + std::to_string(target.layer) + "ba" : tag.substr(0, tag.size() - 1) + "a";
		return tag + agi::format("&H%02X&", color.a);
	}
	return tag + color.GetAssOverrideFormatted();
}

void DialogMangetsuGradient::SaveWorkingState() {
	WorkingState &state = working[CurrentKey()];
	state.stops = stops;
	state.angle = angle;
	state.selected_stop = selected_stop;
	state.existing = existing;
	state.modified = state.modified || dirty;
	state.lock_placement = lock_placement;
	state.placement_rect = placement_rect;
	state.loaded_value = loaded_value;
	if (group == TargetGroup::Border)
		outline_sizes[border_index] = {border_size_x, border_size_y};
}

void DialogMangetsuGradient::LoadWorkingState() {
	auto found = working.find(CurrentKey());
	if (found != working.end()) {
		WorkingState const& state = found->second;
		stops = state.stops;
		angle = state.angle;
		selected_stop = state.selected_stop;
		existing = state.existing;
		dirty = state.modified;
		lock_placement = state.lock_placement;
		placement_rect = state.placement_rect;
		loaded_value = state.loaded_value;
	}
	else {
		ResetDefaultGradient();
		lock_placement = false;
		placement_rect = {};
		auto tag = mangetsu::FindEffectiveGradient(original_text, edit_scope, ScopeTarget(CurrentKey()));
		existing = tag && LoadTagValue(tag.value, mangetsu::IsPlacementGradientTagName(tag.name));
		if (!existing) {
			ResetDefaultGradient();
			lock_placement = false;
			placement_rect = {};
		}
		loaded_value = FormatTagValue();
		dirty = false;
	}
	if (group == TargetGroup::Border) {
		auto size = outline_sizes.find(border_index);
		if (size == outline_sizes.end()) {
			AssStyle *style = context && context->ass && active_line ?
				context->ass->GetStyle(active_line->Style) : nullptr;
			double fallback = border_index == 1 ? (style ? style->outline_w : 2.0) : 0.0;
			std::map<std::string, double> named_sizes;
			if (context && context->ass)
				for (auto const& named_style : context->ass->Styles) {
					std::string key = named_style.name;
					std::transform(key.begin(), key.end(), key.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					named_sizes[key] = border_index == 1 ? named_style.outline_w : 0.0;
				}
			auto read_size = [&](bool x_axis) {
				return mangetsu::EffectiveBorderSize(original_text, edit_scope,
					border_index, x_axis, fallback, named_sizes);
			};
			size = outline_sizes.emplace(border_index,
				std::make_pair(read_size(true), read_size(false))).first;
			original_outline_sizes[border_index] = size->second;
		}
		border_size_x = size->second.first;
		border_size_y = size->second.second;
	}
	if (found == working.end()) SaveWorkingState();
	if (found == working.end()) working[CurrentKey()].exists_in_original = existing;
}

std::string DialogMangetsuGradient::FormatStateValue(TargetKey key, WorkingState const& state) const {
	std::string value = "(" + std::to_string(state.angle);
	for (size_t i = 0; i < state.stops.size(); ++i) {
		value += ",";
		if (i && i + 1 != state.stops.size())
			value += agi::format("%g%%,", state.stops[i].pos);
		value += std::get<2>(key) == 0 ? state.stops[i].color.GetAssOverrideFormatted() :
			FormatAssAlpha(state.stops[i].alpha);
	}
	value += ")";
	if (std::get<0>(key) == 0 && std::get<1>(key) == 1 && std::get<2>(key) == 0 &&
		state.lock_placement && state.placement_rect.valid)
		return mangetsu::FormatPlacementGradientValue(state.placement_rect, value);
	return value;
}

std::string DialogMangetsuGradient::BuildWorkingText(bool include_provisional,
	mangetsu::GradientScope *result_scope) {
	SaveWorkingState();
	std::vector<mangetsu::GradientEdit> edits;
	std::set<int> size_written;
	for (auto const& pair : working) {
		TargetKey key = pair.first;
		WorkingState const& state = pair.second;
		bool provisional = include_provisional && key == CurrentKey() &&
			!state.modified && !state.cleared;
		if (!state.modified && !state.cleared && !provisional) continue;
		if (state.cleared && !state.exists_in_original) continue;
		if (state.lock_placement && !state.placement_rect.valid) continue;
		mangetsu::GradientEdit edit;
		edit.target = ScopeTarget(key);
		edit.fallback = FallbackTag(edit.target);
		if (!state.cleared) {
			std::string tag;
			if (edit.target.kind == mangetsu::GradientTargetKind::Outline)
				tag = "\\" + std::to_string(edit.target.layer) + (edit.target.alpha ? "bga" : "bgrd");
			else if (edit.target.kind == mangetsu::GradientTargetKind::Primary && !edit.target.alpha &&
				state.lock_placement && state.placement_rect.valid)
				tag = "\\pgrd";
			else {
				int index = std::get<1>(key);
				tag = "\\" + std::to_string(index) + (edit.target.alpha ? "gra" : "grd");
			}
			edit.gradient = tag + FormatStateValue(key, state);
		}
		if (edit.target.kind == mangetsu::GradientTargetKind::Outline &&
			(changed_outline_sizes.count(edit.target.layer) || new_outline_layers.count(edit.target.layer)) &&
			!size_written.count(edit.target.layer)) {
			auto size = outline_sizes.at(edit.target.layer);
			std::string p = "\\" + std::to_string(edit.target.layer) + "bs";
			edit.border_x = p + "x" + agi::format("%g", size.first);
			edit.border_y = p + "y" + agi::format("%g", size.second);
			size_written.insert(edit.target.layer);
		}
		edits.push_back(std::move(edit));
	}
	for (int layer : changed_outline_sizes) {
		if (size_written.count(layer)) continue;
		mangetsu::GradientEdit edit;
		edit.target = {mangetsu::GradientTargetKind::Outline, layer, false};
		edit.edit_gradient = false;
		auto size = outline_sizes.at(layer);
		std::string p = "\\" + std::to_string(layer) + "bs";
		edit.border_x = p + "x" + agi::format("%g", size.first);
		edit.border_y = p + "y" + agi::format("%g", size.second);
		edits.push_back(std::move(edit));
	}
	AssStyle *style = context && context->ass && active_line ?
		context->ass->GetStyle(active_line->Style) : nullptr;
	for (auto &edit : edits) {
		if (context && context->ass) {
			for (auto const& style : context->ass->Styles) {
				std::string style_key = style.name;
				std::transform(style_key.begin(), style_key.end(), style_key.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				edit.style_fallbacks[style_key] = FallbackTag(edit.target, &style);
				std::string p = "\\" + std::to_string(edit.target.layer) + "bs";
				std::string size = agi::format("%g", edit.target.layer == 1 ? style.outline_w : 0.0);
				edit.style_border_fallbacks[style_key] = {p + "x" + size, p + "y" + size};
			}
		}
		if (edit.border_x.empty() && edit.border_y.empty()) continue;
		std::string p = "\\" + std::to_string(edit.target.layer) + "bs";
		std::string size_fallback = agi::format("%g",
			edit.target.layer == 1 ? (style ? style->outline_w : 2.0) : 0.0);
		edit.border_fallback_x = p + "x" + size_fallback;
		edit.border_fallback_y = p + "y" + size_fallback;
	}
	return mangetsu::ApplyGradientEdits(original_text, edit_scope, edits, result_scope);
}

void DialogMangetsuGradient::PreviewWorkingText() {
	if (context && context->videoController && active_line)
		context->videoController->PreviewSubtitleText(active_line, BuildWorkingText(true));
}

void DialogMangetsuGradient::SwitchTarget(TargetKey key) {
	CancelAddStopPlacement(false);
	EndPlacementSession();
	SaveWorkingState();
	group = std::get<0>(key) ? TargetGroup::Border : TargetGroup::Main;
	if (group == TargetGroup::Border) border_index = std::get<1>(key);
	else main_index = std::get<1>(key);
	mode = std::get<2>(key) ? ChannelMode::Alpha : ChannelMode::Color;
	LoadWorkingState();
	RefreshControls();
	PreviewWorkingText();
	if (lock_placement && placement_rect.valid) StartPlacementSession();
}

void DialogMangetsuGradient::OnTargetSelected(wxCommandEvent&) {
	if (updating_controls) return;
	int selected = target_list->GetSelection();
	if (selected >= 0 && selected < static_cast<int>(visible_targets.size())) {
		TargetKey key = visible_targets[selected];
		std::get<2>(key) = mode == ChannelMode::Alpha ? 1 : 0;
		SwitchTarget(key);
	}
}

void DialogMangetsuGradient::OnNewOutline(wxCommandEvent&) {
	int next = border_layers.empty() ? 2 : std::max(2, border_layers.back() + 1);
	new_outline_layers.insert(next);
	AssStyle *style = context && context->ass && active_line ? context->ass->GetStyle(active_line->Style) : nullptr;
	double base = style ? style->outline_w : 2.0;
	if (!outline_sizes.empty()) base = std::max(base, outline_sizes.rbegin()->second.first);
	outline_sizes[next] = {base + 2.0, base + 2.0};
	original_outline_sizes[next] = outline_sizes[next];
	RefreshAvailableTargets();
	SwitchTarget({1, next, 0});
}

void DialogMangetsuGradient::OnDeleteOutline(wxCommandEvent&) {
	if (group != TargetGroup::Border || !new_outline_layers.count(border_index)) return;
	CancelAddStopPlacement(false);
	int removed = border_index;
	new_outline_layers.erase(removed);
	changed_outline_sizes.erase(removed);
	outline_sizes.erase(removed);
	original_outline_sizes.erase(removed);
	working.erase(TargetKey{1, removed, 0});
	working.erase(TargetKey{1, removed, 1});
	border_index = 1;
	mode = ChannelMode::Color;
	RefreshAvailableTargets();
	LoadWorkingState();
	RefreshControls();
	PreviewWorkingText();
}

void DialogMangetsuGradient::OnBorderSizeChanged(wxCommandEvent&) {
	if (updating_controls || group != TargetGroup::Border) return;
	border_size_x = border_x_ctrl->GetValue();
	border_size_y = border_y_ctrl->GetValue();
	outline_sizes[border_index] = {border_size_x, border_size_y};
	auto original = original_outline_sizes.find(border_index);
	if (original != original_outline_sizes.end() &&
		std::abs(border_size_x - original->second.first) < 0.001 &&
		std::abs(border_size_y - original->second.second) < 0.001)
		changed_outline_sizes.erase(border_index);
	else
		changed_outline_sizes.insert(border_index);
	SchedulePreview(_("set outline size"), false);
}

void DialogMangetsuGradient::RefreshControls() {
	updating_controls = true;

	angle_ctrl->SetValue(angle);
	target_list->Clear();
	visible_targets.clear();
	auto add_target = [&](std::string const& label, TargetKey key) {
		target_list->Append(to_wx(label));
		visible_targets.push_back(key);
	};
	add_target("Primary", {0, 1, 0});
	add_target("Secondary", {0, 2, 0});
	for (int layer : border_layers)
		add_target(agi::format("Outline %d", layer), {1, layer, 0});
	add_target("Shadow", {0, 4, 0});
	add_target("Fifth", {0, 5, 0});
	for (size_t i = 0; i < visible_targets.size(); ++i)
		if (std::get<0>(visible_targets[i]) == std::get<0>(CurrentKey()) &&
			std::get<1>(visible_targets[i]) == std::get<1>(CurrentKey()))
			target_list->SetSelection(static_cast<int>(i));
	color_button->SetLabel(_("Color"));
	alpha_button->SetLabel(_("Alpha"));

	color_button->SetValue(mode == ChannelMode::Color);
	alpha_button->SetValue(mode == ChannelMode::Alpha);
	editing_label->SetLabel(to_wx("Editing: " + CurrentTargetName() + " — " + CurrentModeName()));
	SetTitle(to_wx("Gradient Editor — " + CurrentTargetName()));
	outline_size_panel->Show(group == TargetGroup::Border);
	delete_outline_button->Enable(group == TargetGroup::Border && new_outline_layers.count(border_index));
	if (group == TargetGroup::Border) {
		border_x_ctrl->SetValue(border_size_x);
		border_y_ctrl->SetValue(border_size_y);
	}
	status_label->SetLabel(to_wx(CurrentStatus()));
	remove_button->Enable(selected_stop > 0 && selected_stop < static_cast<int>(stops.size()) - 1);
	RefreshPlacementControl();

	updating_controls = false;
	if (stop_bar)
		stop_bar->Refresh(false);
	Layout();
}

void DialogMangetsuGradient::RefreshLightControls() {
	updating_controls = true;
	if (angle_ctrl && angle_ctrl->GetValue() != angle)
		angle_ctrl->SetValue(angle);
	if (color_button) {
		if (mode == ChannelMode::Color)
			color_button->SetLabel(existing ? _("Color *") : _("Color"));
		color_button->SetValue(mode == ChannelMode::Color);
	}
	if (alpha_button) {
		if (mode == ChannelMode::Alpha)
			alpha_button->SetLabel(existing ? _("Alpha *") : _("Alpha"));
		alpha_button->SetValue(mode == ChannelMode::Alpha);
	}
	if (status_label)
		status_label->SetLabel(to_wx(CurrentStatus()));
	RefreshPlacementControl();
	if (remove_button)
		remove_button->Enable(selected_stop > 0 && selected_stop < static_cast<int>(stops.size()) - 1);
	updating_controls = false;

	if (stop_bar)
		stop_bar->Refresh(false);
}

TagRef DialogMangetsuGradient::CurrentTag() const {
	if (group == TargetGroup::Main) {
		if (mode == ChannelMode::Color)
			return {"\\" + std::to_string(main_index) + "grd", main_index == 3 ? "\\1bgrd" : "", "\\" + std::to_string(main_index) + "grd"};
		return {"\\" + std::to_string(main_index) + "gra", main_index == 3 ? "\\1bga" : "", "\\" + std::to_string(main_index) + "gra"};
	}

	if (mode == ChannelMode::Color)
		return {"\\" + std::to_string(border_index) + "bgrd", border_index == 1 ? "\\3grd" : "", "\\" + std::to_string(border_index) + "bgrd"};
	return {"\\" + std::to_string(border_index) + "bga", border_index == 1 ? "\\3gra" : "", "\\" + std::to_string(border_index) + "bga"};
}

TagRef DialogMangetsuGradient::PlacementTag() const {
	return {"\\pgrd", "\\1pgrd", "\\pgrd"};
}

TagRef DialogMangetsuGradient::OutputTag() const {
	return PlacementTagActive() ? PlacementTag() : CurrentTag();
}

bool DialogMangetsuGradient::PlacementSupported() const {
	// Keep this small capability table explicit: Mangetsu currently has only
	// primary RGB fixed-frame gradients, but adding a renderer channel later is
	// a one-entry change instead of a rewrite of the dialog.
	struct Capability { TargetGroup group; ChannelMode mode; int main; };
	static Capability const capabilities[] = {
		{TargetGroup::Main, ChannelMode::Color, 1},
	};
	for (auto const& capability : capabilities) {
		if (group == capability.group && mode == capability.mode && main_index == capability.main)
			return true;
	}
	return false;
}

bool DialogMangetsuGradient::PlacementTransformUnsupported() const {
	if (!PlacementSupported() || !active_line || existing)
		return false;
	TagRef attached = CurrentTag();
	TagRef placed = PlacementTag();
	auto has_tag = [&](std::string const& text, TagRef const& tag) {
		return text.find(tag.name) != std::string::npos ||
			(!tag.alias.empty() && text.find(tag.alias) != std::string::npos);
	};
	std::string const& text = active_line->Text.get();
	for (int pos = 0; (pos = static_cast<int>(text.find("\\t(", pos))) >= 0; ++pos) {
		int const open = pos + 2;
		int const close = matching_paren(text, open);
		if (close < 0)
			continue;
		std::string const transform = text.substr(open + 1, close - open - 1);
		if (has_tag(transform, attached) || has_tag(transform, placed))
			return true;
		pos = close;
	}
	return false;
}

bool DialogMangetsuGradient::PlacementTagActive() const {
	return PlacementSupported() && lock_placement && placement_rect.valid;
}

bool DialogMangetsuGradient::HasVideo() const {
	return context && context->videoDisplay && context->videoDisplay->HasVideo();
}

bool DialogMangetsuGradient::IsLockedLine() const {
	return active_line && trim_copy(active_line->Effect.get()) == "LOCK";
}

std::string DialogMangetsuGradient::CurrentGroupName() const {
	return group == TargetGroup::Main ? "Main" : "Outline";
}

std::string DialogMangetsuGradient::CurrentTargetName() const {
	if (group == TargetGroup::Border)
		return agi::format("Outline %d", border_index);

	switch (main_index) {
		case 1: return "Primary";
		case 2: return "Secondary";
		case 3: return "Outline 1";
		case 4: return "Shadow";
		case 5: return "Fifth";
		default: return "Main";
	}
}

std::string DialogMangetsuGradient::CurrentModeName() const {
	return mode == ChannelMode::Color ? "Color" : "Alpha";
}

std::string DialogMangetsuGradient::CurrentStatus() const {
	TagRef tag = OutputTag();
	auto state = working.find(CurrentKey());
	bool original_exists = state != working.end() && state->second.exists_in_original;
	bool pending = (state != working.end() && state->second.modified) ||
		(group == TargetGroup::Border && changed_outline_sizes.count(border_index));
	std::string status = "Editing: " + CurrentGroupName() + " / " + CurrentTargetName() + " / " + CurrentModeName() +
		" -> " + tag.status_name + "\nOriginal gradient: " + (original_exists ? "yes" : "no") +
		"   Pending edit: " + (pending ? "yes" : "no");
	if (!placement_status.empty())
		status += "\n" + from_wx(placement_status);
	if (!add_stop_status.empty())
		status += "\n" + from_wx(add_stop_status);
	if (group == TargetGroup::Border && new_outline_layers.count(border_index) &&
		!changed_outline_sizes.count(border_index) &&
		state != working.end() && !state->second.modified)
		status += "\nPreview only — edit this outline to keep it.";
	return status;
}

void DialogMangetsuGradient::ResetDefaultGradient() {
	angle = 0;
	selected_stop = 0;
	GradientStop start;
	start.pos = 0.0;
	start.color = agi::Color(0, 0, 0);
	start.alpha = 0x00;
	GradientStop end;
	end.pos = 100.0;
	end.color = agi::Color(255, 255, 255);
	end.alpha = 0xFF;
	stops = {start, end};
}

bool DialogMangetsuGradient::LoadTagValue(std::string const& value, bool placement) {
	std::string attached_value = value;
	if (placement) {
		if (!mangetsu::ParsePlacementGradientValue(value, placement_rect, attached_value))
			return false;
		lock_placement = true;
	}
	else {
		placement_rect = {};
		lock_placement = false;
	}

	auto tokens = TokenizeTagValue(attached_value);
	if (tokens.size() < 3)
		return false;

	try {
		angle = std::lround(std::stod(tokens[0]));
	}
	catch (...) {
		return false;
	}
	angle %= 360;
	if (angle < 0)
		angle += 360;

	std::vector<GradientStop> parsed;
	for (size_t i = 1; i < tokens.size(); ++i) {
		double pos = parsed.empty() ? 0.0 : 100.0;
		std::string token = tokens[i];
		if (ends_with_percent(token)) {
			if (i + 1 >= tokens.size())
				return false;
			try {
				pos = std::stod(token.substr(0, token.size() - 1));
			}
			catch (...) {
				return false;
			}
			token = tokens[++i];
		}

		GradientStop stop;
		stop.pos = clamp_percent(pos);
		if (mode == ChannelMode::Color) {
			if (!ParseAssColor(token, stop.color))
				return false;
			stop.alpha = 0;
		}
		else {
			if (!ParseAssAlpha(token, stop.alpha))
				return false;
			stop.color = agi::Color(255, 255, 255);
		}
		parsed.push_back(stop);
	}

	if (parsed.size() < 2)
		return false;
	parsed.front().pos = 0.0;
	parsed.back().pos = 100.0;
	stops = std::move(parsed);
	SortStops();
	selected_stop = 0;
	return true;
}

std::string DialogMangetsuGradient::FormatTagValue() const {
	std::string attached = FormatAttachedTagValue();
	return PlacementTagActive() ? mangetsu::FormatPlacementGradientValue(placement_rect, attached) : attached;
}

std::string DialogMangetsuGradient::FormatAttachedTagValue() const {
	std::string value = "(" + std::to_string(angle);
	for (size_t i = 0; i < stops.size(); ++i) {
		value += ",";
		if (i != 0 && i + 1 != stops.size())
			value += agi::format("%g%%,", stops[i].pos);
		value += mode == ChannelMode::Color ? stops[i].color.GetAssOverrideFormatted() : FormatAssAlpha(stops[i].alpha);
	}
	value += ")";
	return value;
}

void DialogMangetsuGradient::RefreshPlacementControl() {
	if (!lock_placement_button)
		return;

	bool const supported = PlacementSupported();
	if (!supported || PlacementTransformUnsupported()) {
		lock_placement = false;
		lock_placement_button->SetValue(false);
		lock_placement_button->Enable(false);
		lock_placement_button->SetToolTip(supported ?
			_("The current Gradient Editor does not edit gradients inside transforms.") :
			_("Placement gradients currently support only the primary fill color."));
		return;
	}

	lock_placement_button->SetValue(lock_placement);
	bool const interactive = HasVideo();
	lock_placement_button->Enable(interactive || placement_rect.valid);
	if (!interactive && !placement_rect.valid)
		lock_placement_button->SetToolTip(_("A loaded video is required to choose a fixed gradient area."));
	else
		lock_placement_button->SetToolTip(_("Lock the gradient to a fixed area of the video.\nEnable this, then drag the area in the video preview.\nOutside the area, Mangetsu uses the line's normal primary color."));
}

void DialogMangetsuGradient::StartPlacementSession() {
	if (placement_capture_active || !lock_placement || !PlacementSupported())
		return;
	if (IsLockedLine()) {
		placement_status = _("The active line is locked; placement editing is unavailable.");
		RefreshLightControls();
		return;
	}
	if (!HasVideo()) {
		placement_status = _("Load a video to choose or replace the fixed gradient area.");
		RefreshLightControls();
		return;
	}

	auto *display = context->videoDisplay;
	auto previous = display->TakeTool();
	if (!previous) {
		placement_status = _("The video tool is not ready yet. Try again after the video preview has rendered.");
		RefreshLightControls();
		return;
	}

	auto session = std::make_shared<GradientPlacementSession>();
	session->previous_tool = std::move(previous);
	session->original_line = active_line;
	session->rectangle = placement_rect;
	session->pending_gradient_value = FormatTagValue();
	session->accepted = [this](mangetsu::PlacementRect const& rect) { OnPlacementAccepted(rect); };
	session->invalid_drag = [this] { OnPlacementInvalid(); };
	session->cancelled = [this] { OnPlacementCancelled(); };
	session->status = [this](char const* text) {
		placement_status = to_wx(text);
		RefreshLightControls();
	};
	session->deactivated = [this] { OnPlacementToolDeactivated(); };
	placement_session = session;
	placement_capture_active = true;
	display->SetTool(agi::make_unique<VisualToolGradientPlacement>(display, context, session));
	placement_status = placement_rect.valid ? _("Drag on the video to replace the fixed gradient area.") :
		_("Drag on the video to choose the fixed gradient area.");
	RefreshLightControls();
	display->Render();
}

void DialogMangetsuGradient::EndPlacementSession(bool restore_tool) {
	auto session = placement_session;
	placement_session.reset();
	placement_capture_active = false;
	if (!session)
		return;

	session->ending = true;
	session->accepted = nullptr;
	session->invalid_drag = nullptr;
	session->cancelled = nullptr;
	session->status = nullptr;
	session->deactivated = nullptr;

	auto *display = context ? context->videoDisplay : nullptr;
	if (restore_tool && display && display->ToolIsType(typeid(VisualToolGradientPlacement))) {
		auto placement_tool = display->TakeTool();
		// Destroying the temporary tool restores the normal cursor. Do it before
		// reattaching the retained tool so tools such as the crosshair can set
		// their own cursor again.
		placement_tool.reset();
		if (session->previous_tool)
			display->SetTool(std::move(session->previous_tool));
	}
	else
		session->previous_tool.reset();
}

void DialogMangetsuGradient::OnPlacementAccepted(mangetsu::PlacementRect const& rect) {
	if (!lock_placement || !PlacementSupported())
		return;
	placement_rect = rect;
	if (placement_session)
		placement_session->rectangle = rect;
	if (placement_session)
		placement_session->pending_gradient_value = FormatTagValue();
	placement_status = _("Fixed gradient area captured.");
	MarkGradientChanged(true, _("set gradient placement"));
}

void DialogMangetsuGradient::OnPlacementInvalid() {
	placement_status = _("Drag a non-zero rectangle in the visible video area.");
	RefreshLightControls();
}

void DialogMangetsuGradient::OnPlacementCancelled() {
	// The tool can be on its own event stack while this callback fires. Queue
	// restoration until it returns so it is never destroyed from inside its own
	// mouse/key handler.
	CallAfter([this] {
		EndPlacementSession();
		placement_status = _("Placement selection cancelled.");
		RefreshLightControls();
	});
}

void DialogMangetsuGradient::OnPlacementToolDeactivated() {
	if (!placement_session || placement_session->ending)
		return;
	placement_session->previous_tool.reset();
	placement_session.reset();
	placement_capture_active = false;
	placement_status = _("Placement selection ended because the video tool changed or the video was closed.");
	RefreshLightControls();
}

void DialogMangetsuGradient::OnActiveLineChanged(AssDialogue *line) {
	if (line == active_line)
		return;
	CancelAddStopPlacement(false);
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	CallAfter([this] {
		EndPlacementSession();
		if (context && context->videoController && active_line)
			context->videoController->PreviewSubtitleText(active_line, original_text);
		Destroy();
	});
}

void DialogMangetsuGradient::OnFileCommit(int type, AssDialogue const* changed) {
	bool new_file = type == AssFile::COMMIT_NEW;
	if (!new_file &&
		(changed != active_line || committing || !active_line || active_line->Text.get() == original_text))
		return;
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	CallAfter([this, new_file] {
		EndPlacementSession();
		// A new file or an external edit invalidates the working snapshot.
		if (!new_file && context && context->videoController && active_line && context->ass &&
			context->selectionController->GetActiveLine() == active_line)
			context->videoController->PreviewSubtitleText(active_line, active_line->Text.get());
		Destroy();
	});
}

void DialogMangetsuGradient::OnPlacementToggle(wxCommandEvent&) {
	if (updating_controls || !lock_placement_button)
		return;
	bool const requested_lock = lock_placement_button->GetValue();
	CancelAddStopPlacement(false);

	bool const was_placed = PlacementTagActive();
	if (!requested_lock) {
		EndPlacementSession();
		lock_placement = false;
		placement_status = _("Gradient placement unlocked.");
		if (was_placed || dirty)
			MarkGradientChanged(true, _("unlock gradient placement"));
		else
			RefreshLightControls();
		return;
	}

	if (!PlacementSupported() || PlacementTransformUnsupported()) {
		lock_placement = false;
		placement_status = PlacementTransformUnsupported() ?
			_("Placement gradients inside transforms are not editable in this dialog.") : wxString();
		RefreshLightControls();
		return;
	}
	if (IsLockedLine()) {
		lock_placement = false;
		placement_status = _("The active line is locked; placement editing is unavailable.");
		RefreshLightControls();
		return;
	}
	if (!HasVideo() && !placement_rect.valid) {
		lock_placement = false;
		placement_status = _("Load a video before choosing a fixed gradient area.");
		RefreshLightControls();
		return;
	}

	lock_placement = true;
	if (placement_rect.valid)
		MarkGradientChanged(true, _("lock gradient placement"));
	else
		RefreshLightControls();
	StartPlacementSession();
}

void DialogMangetsuGradient::UpdateDirtyState() {
	dirty = FormatTagValue() != loaded_value;
}

void DialogMangetsuGradient::MarkGradientChanged(bool immediate, wxString const& message) {
	UpdateDirtyState();
	working[CurrentKey()].cleared = false;
	working[CurrentKey()].modified = true;
	if (placement_session)
		placement_session->pending_gradient_value = FormatTagValue();
	RefreshLightControls();
	SchedulePreview(message.IsEmpty() ? _("set gradient") : message, immediate);
}

void DialogMangetsuGradient::SchedulePreview(wxString const& message, bool immediate) {
	(void)message;
	if (immediate) {
		FlushPreview();
		return;
	}

	if (!preview_pending) {
		preview_pending = true;
		preview_timer.Start(33, wxTIMER_ONE_SHOT);
	}
}

void DialogMangetsuGradient::FlushPreview() {
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}

	PreviewWorkingText();
	RefreshLightControls();
}

void DialogMangetsuGradient::OnPreviewTimer(wxTimerEvent&) {
	preview_pending = false;
	FlushPreview();
}

void DialogMangetsuGradient::OnDialogKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && stop_bar && stop_bar->IsAdding()) {
		CancelAddStopPlacement();
		return;
	}
	event.Skip();
}

void DialogMangetsuGradient::ClearCurrent() {
	SaveWorkingState();
	WorkingState &state = working[CurrentKey()];
	state.cleared = true;
	state.modified = true;
	dirty = true;
	existing = false;
	PreviewWorkingText();
	RefreshControls();
}

void DialogMangetsuGradient::ReverseStops() {
	angle = (angle + 180) % 360;
	int old_selected = selected_stop;
	for (auto& stop : stops)
		stop.pos = 100.0 - stop.pos;
	std::reverse(stops.begin(), stops.end());
	SortStops();
	selected_stop = std::max(0, static_cast<int>(stops.size()) - 1 - old_selected);
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::StartAddStopPlacement() {
	if (!stop_bar || !add_button)
		return;
	if (stop_bar->IsAdding()) {
		CancelAddStopPlacement();
		return;
	}

	stop_bar->BeginAddPlacement();
	add_button->SetLabel(_("Cancel Add"));
	add_stop_status = _("Add stop: click or drag on the gradient strip; Esc or right-click cancels.");
	RefreshLightControls();
	Layout();
}

void DialogMangetsuGradient::CancelAddStopPlacement(bool show_status) {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	if (add_button)
		add_button->SetLabel(_("+ Stop"));
	add_stop_status = show_status ? _("Stop placement cancelled.") : wxString();
	RefreshLightControls();
	Layout();
}

void DialogMangetsuGradient::FinishAddStopPlacement(double pos) {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	if (add_button)
		add_button->SetLabel(_("+ Stop"));
	add_stop_status = wxString::Format(_("Stop placed at %.1f%%."), clamp_percent(pos));
	AddStop(pos);
	Layout();
}

void DialogMangetsuGradient::AddStop(double pos) {
	GradientStop stop = SampleAt(pos);
	stop.pos = clamp_percent(pos);
	stops.push_back(stop);
	SortStops();
	// SortStops is stable, so the newly appended stop is the last matching
	// position even when the user deliberately places it over another stop.
	for (size_t i = stops.size(); i-- > 0;) {
		if (std::abs(stops[i].pos - stop.pos) < 0.01) {
			selected_stop = static_cast<int>(i);
			break;
		}
	}
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::RemoveSelectedStop() {
	if (selected_stop <= 0 || selected_stop >= static_cast<int>(stops.size()) - 1)
		return;
	stops.erase(stops.begin() + selected_stop);
	selected_stop = std::min<int>(selected_stop, static_cast<int>(stops.size()) - 1);
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::FlatZone() {
	if (selected_stop < 0 || selected_stop >= static_cast<int>(stops.size()))
		return;

	GradientStop dup = stops[selected_stop];
	if (selected_stop == 0)
		dup.pos = std::min(99.0, stops[selected_stop].pos + 5.0);
	else if (selected_stop == static_cast<int>(stops.size()) - 1)
		dup.pos = std::max(1.0, stops[selected_stop].pos - 5.0);
	else
		dup.pos = std::min(99.0, stops[selected_stop].pos + 3.0);
	stops.push_back(dup);
	SortStops();
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::EditSelectedStop() {
	if (selected_stop < 0 || selected_stop >= static_cast<int>(stops.size()))
		return;
	SaveWorkingState();
	WorkingState before = working[CurrentKey()];
	bool was_dirty = dirty;

	if (mode == ChannelMode::Color) {
		agi::Color selected = stops[selected_stop].color;
		bool ok = GetColorFromUser(this, selected, false, [&](agi::Color new_color) {
			stops[selected_stop].color = new_color;
			MarkGradientChanged(false);
		});
		if (!ok) {
			stops[selected_stop].color = selected;
			working[CurrentKey()] = before;
			dirty = was_dirty;
			PreviewWorkingText();
			RefreshLightControls();
		}
		else
			FlushPreview();
	}
	else {
		agi::Color selected(255, 255, 255, static_cast<unsigned char>(stops[selected_stop].alpha));
		bool ok = GetColorFromUser(this, selected, true, [&](agi::Color new_color) {
			stops[selected_stop].alpha = new_color.a;
			MarkGradientChanged(false);
		});
		if (!ok) {
			stops[selected_stop].alpha = selected.a;
			working[CurrentKey()] = before;
			dirty = was_dirty;
			PreviewWorkingText();
			RefreshLightControls();
		}
		else
			FlushPreview();
	}
}

GradientStop DialogMangetsuGradient::SampleAt(double pos) const {
	if (stops.empty())
		return GradientStop();
	pos = clamp_percent(pos);

	for (size_t i = 1; i < stops.size(); ++i) {
		if (pos <= stops[i].pos) {
			GradientStop left = stops[i - 1];
			GradientStop right = stops[i];
			double span = std::max(0.0001, right.pos - left.pos);
			double t = (pos - left.pos) / span;
			GradientStop out;
			out.pos = pos;
			out.color = lerp_color(left.color, right.color, t);
			out.alpha = clamp_alpha(std::lround(left.alpha + (right.alpha - left.alpha) * t));
			return out;
		}
	}
	GradientStop out = stops.back();
	out.pos = pos;
	return out;
}

void DialogMangetsuGradient::SortStops() {
	std::stable_sort(stops.begin(), stops.end(), [](GradientStop const& a, GradientStop const& b) {
		return a.pos < b.pos;
	});
	if (!stops.empty()) {
		stops.front().pos = 0.0;
		stops.back().pos = 100.0;
	}
}

static int matching_paren(std::string const& text, int open) {
	int depth = 0;
	for (int i = open; i < static_cast<int>(text.size()); ++i) {
		if (text[i] == '(')
			++depth;
		else if (text[i] == ')') {
			--depth;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

std::vector<std::string> DialogMangetsuGradient::TokenizeTagValue(std::string const& value) {
	std::vector<std::string> tokens;
	if (value.size() < 2 || value.front() != '(' || value.back() != ')')
		return tokens;

	int depth = 0;
	size_t start = 1;
	for (size_t i = 1; i + 1 < value.size(); ++i) {
		if (value[i] == '(')
			++depth;
		else if (value[i] == ')')
			--depth;
		else if (value[i] == ',' && depth == 0) {
			tokens.push_back(trim_copy(value.substr(start, i - start)));
			start = i + 1;
		}
	}
	tokens.push_back(trim_copy(value.substr(start, value.size() - 1 - start)));
	return tokens;
}

bool DialogMangetsuGradient::ParseAssColor(std::string const& text, agi::Color& color) {
	std::string s = trim_copy(text);
	size_t pos = s.find("&H");
	if (pos == std::string::npos)
		pos = s.find("&h");
	if (pos == std::string::npos)
		return false;
	pos += 2;
	size_t end = pos;
	while (end < s.size() && std::isxdigit(static_cast<unsigned char>(s[end])))
		++end;
	if (end == pos)
		return false;
	unsigned value = static_cast<unsigned>(std::stoul(s.substr(pos, end - pos), nullptr, 16));
	color = agi::Color(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
	return true;
}

bool DialogMangetsuGradient::ParseAssAlpha(std::string const& text, int& alpha) {
	std::string s = trim_copy(text);
	size_t pos = s.find("&H");
	if (pos == std::string::npos)
		pos = s.find("&h");
	if (pos == std::string::npos)
		return false;
	pos += 2;
	size_t end = pos;
	while (end < s.size() && std::isxdigit(static_cast<unsigned char>(s[end])))
		++end;
	if (end == pos)
		return false;
	alpha = clamp_alpha(static_cast<int>(std::stoul(s.substr(pos, end - pos), nullptr, 16)));
	return true;
}

std::string DialogMangetsuGradient::FormatAssAlpha(int alpha) {
	return agi::format("&H%02X&", clamp_alpha(alpha));
}

wxRect DialogMangetsuGradient::BarRect() const {
	wxSize size = stop_bar->GetClientSize();
	return wxRect(82, 58, std::max(120, size.GetWidth() - 164), 34);
}

wxRect DialogMangetsuGradient::StartSwatchRect() const {
	return wxRect(18, 48, 44, 54);
}

wxRect DialogMangetsuGradient::EndSwatchRect() const {
	wxSize size = stop_bar->GetClientSize();
	return wxRect(size.GetWidth() - 62, 48, 44, 54);
}

int DialogMangetsuGradient::HitTestStop(wxPoint pos) const {
	wxRect bar = BarRect();
	for (int i = static_cast<int>(stops.size()) - 1; i >= 0; --i) {
		int x = bar.GetX() + static_cast<int>(std::lround(bar.GetWidth() * stops[i].pos / 100.0));
		if (std::abs(pos.x - x) <= 10 && pos.y >= bar.GetY() - 42 && pos.y <= bar.GetBottom() + 12)
			return i;
	}
	return -1;
}

double DialogMangetsuGradient::PosFromMouse(wxPoint pos) const {
	wxRect bar = BarRect();
	if (bar.GetWidth() <= 0)
		return 0.0;
	return clamp_percent(100.0 * (pos.x - bar.GetX()) / bar.GetWidth());
}

void DialogMangetsuGradient::OnAngleChanged(wxCommandEvent&) {
	if (updating_controls)
		return;
	angle = angle_ctrl->GetValue() % 360;
	MarkGradientChanged(false);
}

void DialogMangetsuGradient::OnQuickAngle(int new_angle) {
	angle = ((new_angle % 360) + 360) % 360;
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::OnMode(ChannelMode new_mode) {
	if (updating_controls)
		return;
	TargetKey key = CurrentKey();
	std::get<2>(key) = new_mode == ChannelMode::Alpha ? 1 : 0;
	SwitchTarget(key);
}

void DialogMangetsuGradient::OnApply(wxCommandEvent&) {
	if (stop_bar && stop_bar->IsAdding())
		CancelAddStopPlacement(false);
	if (lock_placement && !placement_rect.valid) {
		placement_status = _("Drag a fixed gradient area before applying placement mode.");
		RefreshLightControls();
		return;
	}
	if (!active_line || !context || !context->ass) return;
	if (preview_pending) { preview_timer.Stop(); preview_pending = false; }
	mangetsu::GradientScope applied_scope;
	std::string result = BuildWorkingText(false, &applied_scope);
	if (result != original_text) {
		active_line->Text = result;
		committing = true;
		context->ass->Commit(_("set gradient"), AssFile::COMMIT_DIAG_TEXT, -1, active_line);
		committing = false;
		original_text = result;
		edit_scope = applied_scope;
		if (context->textSelectionController) {
			if (edit_scope.selected)
				context->textSelectionController->SetSelection(
					static_cast<int>(edit_scope.start), static_cast<int>(edit_scope.end));
			else
				context->textSelectionController->SetInsertionPoint(static_cast<int>(edit_scope.start));
		}
	}
	if (context->videoController)
		context->videoController->PreviewSubtitleText(active_line, original_text);
	working.clear();
	outline_sizes.clear();
	original_outline_sizes.clear();
	changed_outline_sizes.clear();
	new_outline_layers.clear();
	RefreshAvailableTargets();
	if (group == TargetGroup::Border &&
		std::find(border_layers.begin(), border_layers.end(), border_index) == border_layers.end())
		border_index = 1;
	LoadWorkingState();
	RefreshControls();
}

void DialogMangetsuGradient::OnOK(wxCommandEvent&) {
	CancelAddStopPlacement(false);
	if (lock_placement && !placement_rect.valid) return;
	wxCommandEvent apply;
	OnApply(apply);
	EndPlacementSession();
	FinishDialog(wxID_OK);
}

void DialogMangetsuGradient::OnCancel(wxCommandEvent&) {
	CancelAddStopPlacement(false);
	EndPlacementSession();
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	if (context && context->videoController && active_line)
		context->videoController->PreviewSubtitleText(active_line, original_text);
	if (context && context->videoDisplay)
		context->videoDisplay->Render();
	FinishDialog(wxID_CANCEL);
}

void DialogMangetsuGradient::FinishDialog(int result) {
	if (IsModal())
		EndModal(result);
	else
		Destroy();
}

}

void ShowMangetsuGradientDialog(agi::Context *c) {
	if (!c || !c->selectionController || !c->selectionController->GetActiveLine())
		return;

	auto *dlg = new DialogMangetsuGradient(c->parent, c);
	dlg->Show();
}
