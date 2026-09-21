// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "video_box.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_tag_edit.h"
#include "compat.h"
#include "dialogs.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/toolbar.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "text_selection_controller.h"
#include "toast_popup.h"
#include "video_controller.h"
#include "video_display.h"
#include "video_slider.h"

#include <libaegisub/parser.h>

#include <boost/range/algorithm/binary_search.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <wx/clipbrd.h>
#include <wx/cursor.h>
#include <wx/dataobj.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

namespace {

agi::Color InitialFadeColor(std::string const& text, agi::ass::FadeSide side) {
	std::string value = agi::ass::GetFadeColor(text, side);
	auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
	value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
	if (value.size() >= 2 && value[value.size() - 2] == '+' &&
		(value.back() == 'a' || value.back() == 'A'))
		value.resize(value.size() - 2);

	agi::Color color;
	if (!agi::parser::parse(color, value))
		return agi::Color();
	return color;
}

}

VideoBox::VideoBox(wxWindow *parent, bool isDetached, agi::Context *context)
: wxPanel(parent, -1)
, context(context)
{
	auto videoSlider = new VideoSlider(this, context);
	videoSlider->SetToolTip(_("Seek video"));

	auto mainToolbar = toolbar::GetToolbar(this, "video", context, "Video", false);

	VideoPosition = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(80, -1), wxTE_READONLY);
	VideoPosition->SetMinSize(wxSize(60, -1));
	VideoPosition->SetToolTip(_("Current frame time and number"));

	VideoSubsPos = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(80, -1), wxTE_READONLY);
	VideoSubsPos->SetMinSize(wxSize(60, -1));
	VideoSubsPos->SetToolTip(_("Time of this frame relative to start and end of current subs. Right-click for copy, insert, and fade actions."));
	VideoSubsPos->SetCursor(wxCursor(wxCURSOR_HAND));
	VideoSubsPos->Bind(wxEVT_LEFT_DOWN, &VideoBox::OnSubsReadoutClick, this);
	VideoSubsPos->Bind(wxEVT_CONTEXT_MENU, &VideoBox::OnSubsReadoutContextMenu, this);

	static const double playback_speeds[] = {
		0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 2.00,
		2.25, 2.50, 2.75, 3.00, 3.25, 3.50, 3.75, 4.00
	};
	static wxString playback_speed_labels[WXSIZEOF(playback_speeds)];
	static bool playback_labels_init = false;
	if (!playback_labels_init) {
		for (size_t i = 0; i < WXSIZEOF(playback_speeds); ++i)
			playback_speed_labels[i] = wxString::Format("%.2fx", playback_speeds[i]);
		playback_labels_init = true;
	}

	wxArrayString playback_speed_choices;
	for (auto const& label : playback_speed_labels)
		playback_speed_choices.Add(label);

	VideoPlaybackSpeed = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize, playback_speed_choices);
	VideoPlaybackSpeed->SetToolTip(_("Video playback speed"));
	VideoPlaybackSpeed->SetMinSize(wxSize(70, -1));

	auto playback_speed_to_index = [](double speed) {
		int best = 2; // 1.00x
		double best_dist = std::abs(speed - playback_speeds[best]);
		for (int i = 0; i < (int)WXSIZEOF(playback_speeds); ++i) {
			double dist = std::abs(speed - playback_speeds[i]);
			if (dist < best_dist) {
				best = i;
				best_dist = dist;
			}
		}
		return best;
	};

	VideoPlaybackSpeed->SetSelection(playback_speed_to_index(context->videoController->GetPlaybackSpeed()));
	VideoPlaybackSpeed->Bind(wxEVT_CHOICE, [=](wxCommandEvent&) {
		int sel = VideoPlaybackSpeed->GetSelection();
		if (sel >= 0 && sel < (int)WXSIZEOF(playback_speeds))
			context->videoController->SetPlaybackSpeed(playback_speeds[sel]);
	});

	wxArrayString choices;
	for (int i = 1; i <= 24; ++i)
		choices.Add(fmt_wx("%g%%", i * 12.5));
	auto zoomBox = new wxComboBox(this, -1, "75%", wxDefaultPosition, wxDefaultSize, choices, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);

	auto visualToolBar = toolbar::GetToolbar(this, "visual_tools", context, "Video", true);
	auto visualSubToolBar = new wxToolBar(this, -1, wxDefaultPosition, wxDefaultSize, wxTB_VERTICAL | wxTB_BOTTOM | wxTB_NODIVIDER | wxTB_FLAT);

	auto videoDisplay = new VideoDisplay(visualSubToolBar, isDetached, zoomBox, this, context);
	videoDisplay->MoveBeforeInTabOrder(videoSlider);

	auto toolbarSizer = new wxBoxSizer(wxVERTICAL);
	toolbarSizer->Add(visualToolBar, wxSizerFlags(1));
	toolbarSizer->Add(visualSubToolBar, wxSizerFlags());

	auto topSizer = new wxBoxSizer(wxHORIZONTAL);
	topSizer->Add(toolbarSizer, 0, wxEXPAND);
	topSizer->Add(videoDisplay, isDetached, isDetached ? wxEXPAND : 0);

	auto videoBottomSizer = new wxBoxSizer(wxHORIZONTAL);
	videoBottomSizer->Add(mainToolbar, wxSizerFlags(0).Center());
	videoBottomSizer->Add(VideoPosition, wxSizerFlags(1).Center().Border(wxLEFT));
	videoBottomSizer->Add(VideoSubsPos, wxSizerFlags(1).Center().Border(wxLEFT));
	videoBottomSizer->Add(VideoPlaybackSpeed, wxSizerFlags(0).Center().Border(wxLEFT));
	videoBottomSizer->Add(zoomBox, wxSizerFlags(0).Center().Border(wxLEFT | wxRIGHT));

	auto VideoSizer = new wxBoxSizer(wxVERTICAL);
	VideoSizer->Add(topSizer, 1, wxEXPAND, 0);
	VideoSizer->Add(new wxStaticLine(this), 0, wxEXPAND, 0);
	VideoSizer->Add(videoSlider, 0, wxEXPAND, 0);
	VideoSizer->Add(videoBottomSizer, 0, wxEXPAND | wxBOTTOM, 5);
	SetSizer(VideoSizer);

	UpdateTimeBoxes();

	connections = agi::signal::make_vector({
		context->ass->AddCommitListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddKeyframesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddTimecodesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddVideoProviderListener(&VideoBox::UpdateTimeBoxes, this),
		context->selectionController->AddSelectionListener(&VideoBox::UpdateTimeBoxes, this),
		context->videoController->AddSeekListener(&VideoBox::UpdateTimeBoxes, this),
		context->videoController->AddPlaybackSpeedListener([=](double speed) {
			if (!VideoPlaybackSpeed) return;
			int new_sel = playback_speed_to_index(speed);
			if (new_sel != VideoPlaybackSpeed->GetSelection())
				VideoPlaybackSpeed->SetSelection(new_sel);
		}),
	});
}

void VideoBox::UpdateTimeBoxes() {
	subs_offset_readout_.clear();
	subs_remaining_readout_.clear();
	if (!context->project->VideoProvider()) return;

	int frame = context->videoController->GetFrameN();
	int time = context->videoController->TimeAtFrame(frame, agi::vfr::EXACT);

	// Set the text box for frame number and time
	VideoPosition->SetValue(fmt_wx("%s - %d", agi::Time(time).GetAssFormatted(true), frame));
	if (boost::binary_search(context->project->Keyframes(), frame)) {
		// Set the background color to indicate this is a keyframe
		VideoPosition->SetBackgroundColour(to_wx(OPT_GET("Colour/Subtitle Grid/Background/Selection")->GetColor()));
		VideoPosition->SetForegroundColour(to_wx(OPT_GET("Colour/Subtitle Grid/Selection")->GetColor()));
	}
	else {
		VideoPosition->SetBackgroundColour(wxNullColour);
		VideoPosition->SetForegroundColour(wxNullColour);
	}

	AssDialogue *active_line = context->selectionController->GetActiveLine();
	if (!active_line) {
		VideoSubsPos->SetValue("");
	}
	else {
		int offset = time - active_line->Start;
		int remaining = time - active_line->End;
		subs_offset_readout_ = fmt_wx("%+dms", offset);
		subs_remaining_readout_ = fmt_wx("%+dms", remaining);
		VideoSubsPos->SetValue(fmt_wx("%s; %s", subs_offset_readout_, subs_remaining_readout_));
	}
}

void VideoBox::OnSubsReadoutClick(wxMouseEvent &event) {
	event.Skip(false);
	wxString value;
	if (GetSubsReadoutForPosition(event.GetPosition(), value))
		HandleReadoutClick(value);
}

void VideoBox::OnSubsReadoutContextMenu(wxContextMenuEvent &event) {
	event.Skip(false);
	wxPoint position = event.GetPosition();
	if (position == wxDefaultPosition)
		position = VideoSubsPos->ScreenToClient(wxGetMousePosition());
	else
		position = VideoSubsPos->ScreenToClient(position);
	wxString value;
	if (!GetSubsReadoutForPosition(position, value))
		return;

	wxString const normalized = NormalizeReadout(value);
	if (normalized.empty())
		return;

	AssDialogue *line = context && context->selectionController ?
		context->selectionController->GetActiveLine() : nullptr;
	int const video_time = context && context->videoController ?
		context->videoController->TimeAtFrame(context->videoController->GetFrameN(), agi::vfr::EXACT) : 0;
	int const fade_in_milliseconds = line ? agi::ass::FadeDurationFromVideoTime(
		agi::ass::FadeSide::In, video_time, line->Start, line->End) : -1;
	int const fade_out_milliseconds = line ? agi::ass::FadeDurationFromVideoTime(
		agi::ass::FadeSide::Out, video_time, line->Start, line->End) : -1;
	bool const fade_available = line && fade_in_milliseconds >= 0 && fade_out_milliseconds >= 0;

	wxMenu menu;
	wxMenuItem *copy = menu.Append(wxID_ANY, _("Copy"));
	wxMenuItem *insert = menu.Append(wxID_ANY, _("Insert at cursor"));
	menu.AppendSeparator();
	wxMenuItem *fade_in = menu.Append(wxID_ANY, _("Fade in from here"));
	wxMenuItem *fade_out = menu.Append(wxID_ANY, _("Fade out from here"));
	fade_in->Enable(fade_available);
	fade_out->Enable(fade_available);

	menu.Bind(wxEVT_MENU, [=](wxCommandEvent& command) {
		if (command.GetId() == copy->GetId()) {
			if (CopyReadoutToClipboard(normalized)) {
				if (context && context->subsEditBox)
					context->subsEditBox->FocusTextCtrl();
				if (!OPT_GET("Video/Disable Click Popup")->GetBool())
					ShowToast(context && context->parent ? context->parent : this, _("Copied to clipboard"));
			}
		}
		else if (command.GetId() == insert->GetId()) {
			if (InsertReadoutIntoEditBox(normalized) && !OPT_GET("Video/Disable Click Popup")->GetBool())
				ShowToast(context && context->parent ? context->parent : this, _("Inserted into edit box"));
		}
		else if (command.GetId() == fade_in->GetId() && fade_available && context && context->selectionController &&
			context->selectionController->GetActiveLine() == line)
			SetFadeFromHere(SubsReadoutKind::Start, fade_in_milliseconds);
		else if (command.GetId() == fade_out->GetId() && fade_available && context && context->selectionController &&
			context->selectionController->GetActiveLine() == line)
			SetFadeFromHere(SubsReadoutKind::End, fade_out_milliseconds);
	});

	VideoSubsPos->PopupMenu(&menu, position);
}

bool VideoBox::GetSubsReadoutForPosition(wxPoint const& position, wxString &value) {
	if (!VideoSubsPos || subs_offset_readout_.IsEmpty() || subs_remaining_readout_.IsEmpty())
		return false;

	wxString current = VideoSubsPos->GetValue();
	if (current.IsEmpty())
		return false;

	wxCoord text_width = 0;
	wxCoord text_height = 0;
	VideoSubsPos->GetTextExtent(subs_offset_readout_ + "; ", &text_width, &text_height);
	wxCoord client_width = VideoSubsPos->GetClientSize().GetWidth();
	if (text_width <= 0 || text_width >= client_width)
		text_width = client_width / 2;

	int x = position.x;
	if (x < 0) x = 0;
	if (client_width > 0 && x > client_width) x = client_width;

	if (x <= text_width) {
		value = subs_offset_readout_;
	}
	else {
		value = subs_remaining_readout_;
	}
	return true;
}

bool VideoBox::SetFadeFromHere(SubsReadoutKind kind, int milliseconds) {
	if (!context || !context->ass || !context->selectionController || milliseconds < 0)
		return false;

	AssDialogue *active = context->selectionController->GetActiveLine();
	if (!active)
		return false;
	agi::ass::FadeSide const side = kind == SubsReadoutKind::Start ?
		agi::ass::FadeSide::In : agi::ass::FadeSide::Out;
	agi::Color chosen = InitialFadeColor(active->Text.get(), side);
	if (!GetColorFromUser(context->parent ? context->parent : this, chosen, false,
		[&](agi::Color color) { chosen = color; }))
		return false;

	int raw_selection_start = 0;
	int raw_selection_end = 0;
	bool restore_selection = false;
	auto const& selected = context->selectionController->GetSelectedSet();
	bool const active_is_target = selected.empty() || selected.count(active);
	if (active_is_target && context->subsEditBox && context->textSelectionController) {
		int const display_start = context->textSelectionController->GetSelectionStart();
		int const display_end = context->textSelectionController->GetSelectionEnd();
		restore_selection = context->subsEditBox->MapDisplayRangeToRaw(
			display_start, display_end, active->Text.get(), raw_selection_start, raw_selection_end);
	}

	bool changed = false;
	auto edit_line = [&](AssDialogue *line) {
		auto result = agi::ass::SetColoredFade(
			line->Text.get(), side, milliseconds, chosen.GetAssOverrideFormatted());
		if (result.text == line->Text.get())
			return;
		if (line == active && restore_selection) {
			raw_selection_start = agi::ass::MoveTextPositionAfterEdit(raw_selection_start,
				result.edit_start, result.edit_end, result.replacement_length);
			raw_selection_end = agi::ass::MoveTextPositionAfterEdit(raw_selection_end,
				result.edit_start, result.edit_end, result.replacement_length);
		}
		line->Text = result.text;
		changed = true;
	};

	if (selected.empty())
		edit_line(active);
	else {
		for (auto line : selected)
			edit_line(line);
	}
	if (!changed)
		return false;

	AssDialogue *commit_line = selected.empty() ? active :
		(selected.size() == 1 ? *selected.begin() : nullptr);
	context->ass->Commit(_("set fade"), AssFile::COMMIT_DIAG_TEXT, -1, commit_line);

	if (restore_selection) {
		context->subsEditBox->SetTextSelection(
			context->subsEditBox->MapRawToDisplay(raw_selection_start, active->Text.get()),
			context->subsEditBox->MapRawToDisplay(raw_selection_end, active->Text.get()));
	}
	if (context->subsEditBox)
		context->subsEditBox->FocusTextCtrl();
	if (context->videoDisplay)
		context->videoDisplay->Render();
	return true;
}

bool VideoBox::HandleReadoutClick(wxString const& value) {
	if (value.IsEmpty() || value == wxS("---"))
		return false;

	wxString normalized = NormalizeReadout(value);
	if (normalized.IsEmpty())
		return false;

	int action = OPT_GET("Video/Click Time Readout Action")->GetInt();
	if (action == 3)
		return false;
	bool want_copy = action == 0 || action == 2;
	bool want_insert = action == 1 || action == 2;

	bool copied = false;
	bool inserted = false;
	if (want_copy)
		copied = CopyReadoutToClipboard(normalized);
	if (want_insert)
		inserted = InsertReadoutIntoEditBox(normalized);

	if (!copied && !inserted)
		return false;

	if (context && context->subsEditBox)
		context->subsEditBox->FocusTextCtrl();

	if (!OPT_GET("Video/Disable Click Popup")->GetBool()) {
		wxWindow *toast_parent = context && context->parent ? context->parent : this;
		if (copied && inserted)
			ShowToast(toast_parent, _("Copied and inserted"));
		else if (copied)
			ShowToast(toast_parent, _("Copied to clipboard"));
		else
			ShowToast(toast_parent, _("Inserted into edit box"));
	}
	return true;
}

bool VideoBox::CopyReadoutToClipboard(wxString const& value) {
	wxClipboard *cb = wxClipboard::Get();
	if (!cb || !cb->Open())
		return false;

	bool ok = cb->SetData(new wxTextDataObject(value));
	if (ok)
		cb->Flush();
	cb->Close();
	return ok;
}

bool VideoBox::InsertReadoutIntoEditBox(wxString const& value) {
	if (!context || !context->subsEditBox)
		return false;
	return context->subsEditBox->InsertTextAtCaret(value);
}

wxString VideoBox::NormalizeReadout(wxString const& value) const {
	wxString trimmed = value;
	trimmed.Trim(true).Trim(false);
	if (trimmed.IsEmpty())
		return wxString();

	if (trimmed.StartsWith("+") || trimmed.StartsWith("-"))
		trimmed = trimmed.Mid(1);

	wxString lower = trimmed.Lower();
	if (lower.EndsWith("ms")) {
		trimmed.Truncate(trimmed.length() - 2);
		trimmed.Trim(true).Trim(false);
	}

	if (trimmed == wxS("---") || trimmed.IsEmpty())
		return wxString();

	return trimmed;
}
