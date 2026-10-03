// A visual authoring tool for Mangetsu's existing native-chat paint state.
#include "dialogs.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "mangetsu_chat_style.h"
#include "options.h"
#include "persist_location.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "text_selection_controller.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/graphics.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace {
using namespace mangetsu;
// The Linux targets still support wxWidgets 3.0. Newer builds can convert
// logical sizes per monitor; older builds use their native pixel coordinates.
template<class T> T DialogPixels(wxWindow const* window, T value) {
#if wxCHECK_VERSION(3, 1, 0)
	return window->FromDIP(value);
#else
	(void)window;
	return value;
#endif
}
enum class Element {
	Panel, Header, Title, LeftName, LeftText, LeftFill, LeftBorder, LeftOutline,
	RightName, RightText, RightFill, RightBorder, RightOutline, Receipt, Count
};
constexpr size_t ElementCount = static_cast<size_t>(Element::Count);
std::array<wxString, ElementCount> Labels() {
	return {_("Phone background"), _("Header background"), _("Contact name"),
		_("Left name"), _("Left text"), _("Left bubble fill"), _("Left bubble border"), _("Left text outline"),
		_("Right name"), _("Right text"), _("Right bubble fill"), _("Right bubble border"), _("Right text outline"), _("Read mark")};
}
ChatStyleTag TagFor(Element element) {
	if (element == Element::Panel) return ChatStyleTag::PanelColor;
	if (element == Element::Header) return ChatStyleTag::Header;
	if (element == Element::Title) return ChatStyleTag::Title;
	if (element >= Element::LeftName && element <= Element::LeftOutline) return ChatStyleTag::Left;
	if (element >= Element::RightName && element <= Element::RightOutline) return ChatStyleTag::Right;
	return ChatStyleTag::ReadMark;
}
agi::Color* ColorFor(ChatAppearance& appearance, Element element) {
	if (element == Element::Panel) return &appearance.panel;
	if (element == Element::Header) return &appearance.header;
	if (element == Element::Title) return &appearance.title;
	bool left = element >= Element::LeftName && element <= Element::LeftOutline;
	auto& side = left ? appearance.left : appearance.right;
	int field = static_cast<int>(element) - static_cast<int>(left ? Element::LeftName : Element::RightName);
	switch (field) {
		case 0: return &side.name;
		case 1: return &side.text;
		case 2: return &side.fill;
		case 3: return &side.border;
		case 4: return &side.outline;
		default: return nullptr;
	}
}
double* SizeFor(ChatAppearance& appearance, Element element) {
	if (element == Element::LeftBorder) return &appearance.left.border_size;
	if (element == Element::LeftOutline) return &appearance.left.outline_size;
	if (element == Element::RightBorder) return &appearance.right.border_size;
	if (element == Element::RightOutline) return &appearance.right.outline_size;
	return nullptr;
}
wxColour PaintColor(agi::Color const& color) { return wxColour(color.r, color.g, color.b, 255 - color.a); }

class ChatPreview final : public wxPanel {
	ChatAppearance const& appearance;
	Element selected = Element::Panel;
	std::array<wxToggleButton*, ElementCount> callouts{};
	std::array<wxPoint, ElementCount> targets{};
	wxRect phone;
	std::function<void(Element)> select;
	void Arrange() {
		auto size = GetClientSize();
		int margin = DialogPixels(this, 12), width = DialogPixels(this, 165);
		int h = std::max(1, size.y - 2 * margin), left = margin + width + DialogPixels(this, 35);
		phone = wxRect(left, margin, std::max(1, size.x - 2 * left), h);
		// Native, keyboard-accessible controls flank the geometry they edit.
		for (size_t i = 0; i < callouts.size(); ++i) {
			bool on_left = i <= static_cast<size_t>(Element::LeftOutline);
			double row;
			if (on_left) {
				static double const rows[] = {.28, .02, .14, .42, .54, .66, .78, .90};
				row = rows[i];
			}
			else {
				static double const rows[] = {.42, .54, .66, .78, .90, .28};
				row = rows[i - static_cast<size_t>(Element::RightName)];
			}
			callouts[i]->SetSize(on_left ? margin : size.x - margin - width,
				margin + static_cast<int>(row * h), width, DialogPixels(this, 30));
		}
		Refresh();
	}
	void Paint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour())); dc.Clear();
		std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
		if (!g) return;
		double x = phone.x, y = phone.y, w = phone.width, h = phone.height;
		// Checkerboard under the phone makes ASS transparency unambiguous.
		g->SetPen(*wxTRANSPARENT_PEN);
		int cell = DialogPixels(this, 12);
		for (int cy = phone.y; cy < phone.GetBottom(); cy += cell)
			for (int cx = phone.x; cx < phone.GetRight(); cx += cell) {
				g->SetBrush(wxBrush(((cx - phone.x) / cell + (cy - phone.y) / cell) % 2 ? wxColour(185, 185, 185) : wxColour(235, 235, 235)));
				g->DrawRectangle(cx, cy, std::min(cell, phone.GetRight() - cx), std::min(cell, phone.GetBottom() - cy));
			}
		g->SetBrush(wxBrush(PaintColor(appearance.panel))); g->DrawRectangle(x, y, w, h);
		g->SetBrush(wxBrush(PaintColor(appearance.header))); g->DrawRectangle(x, y, w, h * .18);
		wxFont font = GetFont(); font.SetPointSize(12); font.SetWeight(wxFONTWEIGHT_BOLD);
		g->SetFont(font, PaintColor(appearance.title));
		double tw, th;
		g->GetTextExtent(wxS("Miku"), &tw, &th); g->DrawText(wxS("Miku"), x + (w - tw) / 2, y + (h * .18 - th) / 2);
		targets[static_cast<size_t>(Element::Header)] = wxPoint(x + w * .12, y + h * .07);
		targets[static_cast<size_t>(Element::Title)] = wxPoint(x + w * .5, y + h * .09);
		targets[static_cast<size_t>(Element::Panel)] = wxPoint(x + w * .10, y + h * .27);
		auto bubble = [&](ChatBubbleStyle const& style, bool left) {
			double bx = x + w * (left ? .07 : .29), by = y + h * (left ? .40 : .73);
			double bw = w * .64, bh = h * .18;
			size_t base = static_cast<size_t>(left ? Element::LeftName : Element::RightName);
			font.SetWeight(wxFONTWEIGHT_NORMAL); font.SetPointSize(10);
			g->SetFont(font, PaintColor(style.name));
			wxString speaker = left ? wxS("Yurf") : wxS("Miku");
			g->GetTextExtent(speaker, &tw, &th);
			double nx = left ? bx : bx + bw - tw;
			g->DrawText(speaker, nx, by - th - DialogPixels(this, 7));
			targets[base] = wxPoint(nx + tw / 2, by - th / 2 - DialogPixels(this, 7));
			// This is representative geometry, not a renderer. A logarithmic
			// width keeps even very large script-coordinate sizes inspectable.
			double border = std::log1p(style.border_size) * DialogPixels(this, 2);
			g->SetPen(border > 0 ? wxPen(PaintColor(style.border), border) : *wxTRANSPARENT_PEN);
			g->SetBrush(wxBrush(PaintColor(style.fill))); g->DrawRoundedRectangle(bx, by, bw, bh, DialogPixels(this, 10));
			font.SetPointSize(11);
			double tx = bx + DialogPixels(this, 12), ty = by + DialogPixels(this, 12);
			wxString message = left ? wxS("Hey, are\nyou there?") : wxS("Yeah");
			double outline = std::log1p(style.outline_size) * DialogPixels(this, 1);
			// Draw each line explicitly; wxGraphicsContext::DrawText is single-line.
			auto draw_message = [&](wxColour color, double dx, double dy) {
				g->SetFont(font, color);
				if (left) {
					g->DrawText(wxS("Hey, are"), tx + dx, ty + dy);
					g->GetTextExtent(wxS("Hey, are"), &tw, &th);
					g->DrawText(wxS("you there?"), tx + dx, ty + th + dy);
				}
				else g->DrawText(message, tx + dx, ty + dy);
			};
			if (outline > 0) for (int i = 0; i < 12; ++i) {
				double angle = i * 6.283185307179586 / 12;
				draw_message(PaintColor(style.outline), std::cos(angle) * outline, std::sin(angle) * outline);
			}
			draw_message(PaintColor(style.text), 0, 0);
			targets[base + 1] = wxPoint(tx + DialogPixels(this, 18), ty + DialogPixels(this, 8));
			targets[base + 2] = wxPoint(bx + bw * .75, by + bh * .45);
			targets[base + 3] = wxPoint(left ? bx : bx + bw, by + bh * .80);
			targets[base + 4] = wxPoint(tx + DialogPixels(this, 6), ty + DialogPixels(this, 16));
			if (!left) {
				double rx = bx + bw - DialogPixels(this, 27), ry = by + bh - DialogPixels(this, 13);
				g->SetPen(wxPen(PaintColor(style.text), DialogPixels(this, 2)));
				for (int i = 0; i < appearance.read_mark; ++i) {
					auto path = g->CreatePath(); double xx = rx + i * DialogPixels(this, 9);
					path.MoveToPoint(xx, ry); path.AddLineToPoint(xx + DialogPixels(this, 4), ry + DialogPixels(this, 4));
					path.AddLineToPoint(xx + DialogPixels(this, 11), ry - DialogPixels(this, 5)); g->StrokePath(path);
				}
				targets[static_cast<size_t>(Element::Receipt)] = wxPoint(rx + DialogPixels(this, 9), ry);
			}
		};
		bubble(appearance.left, true); bubble(appearance.right, false);
		g->SetBrush(*wxTRANSPARENT_BRUSH); g->SetPen(wxPen(GetForegroundColour(), DialogPixels(this, 1)));
		g->DrawRoundedRectangle(x, y, w, h, DialogPixels(this, 10));
		// The active callout stands out; inactive leaders stay deliberately quiet.
		for (size_t i = 0; i < callouts.size(); ++i) {
			auto rect = callouts[i]->GetRect(); bool left = i <= static_cast<size_t>(Element::LeftOutline);
			double sx = left ? rect.GetRight() : rect.GetLeft(), sy = rect.y + rect.height / 2;
			auto target = targets[i]; bool active = i == static_cast<size_t>(selected);
			g->SetPen(wxPen(active ? wxColour(0, 120, 215) : wxColour(140, 140, 140), DialogPixels(this, active ? 2 : 1)));
			auto path = g->CreatePath(); path.MoveToPoint(sx, sy);
			path.AddLineToPoint(left ? x - DialogPixels(this, 12) : x + w + DialogPixels(this, 12), sy);
			path.AddLineToPoint(target.x, target.y); g->StrokePath(path);
			g->SetBrush(wxBrush(active ? wxColour(0, 120, 215) : wxColour(140, 140, 140)));
			g->DrawEllipse(target.x - DialogPixels(this, 2), target.y - DialogPixels(this, 2), DialogPixels(this, 4), DialogPixels(this, 4));
		}
	}
public:
	ChatPreview(wxWindow* parent, ChatAppearance const& appearance, std::function<void(Element)> select)
	: wxPanel(parent), appearance(appearance), select(std::move(select)) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(DialogPixels(this, wxSize(820, 420)));
		auto labels = Labels();
		for (size_t i = 0; i < callouts.size(); ++i) {
			callouts[i] = new wxToggleButton(this, wxID_ANY, labels[i]);
			callouts[i]->Bind(wxEVT_TOGGLEBUTTON, [this, i](wxCommandEvent&) { Select(static_cast<Element>(i)); this->select(selected); });
		}
		Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { Arrange(); event.Skip(); });
		Bind(wxEVT_PAINT, &ChatPreview::Paint, this);
		Select(Element::Panel);
	}
	void Select(Element element) {
		selected = element;
		for (size_t i = 0; i < callouts.size(); ++i) callouts[i]->SetValue(i == static_cast<size_t>(selected));
		Refresh();
	}
};

class ChatStyleDialog final : public wxDialog {
	agi::Context* context;
	ChatAppearance defaults;
	ChatStyleState state;
	ChatStyleMask changed;
	ChatStylePresets presets;
	Element selected = Element::Panel;
	ChatPreview* preview;
	wxChoice* preset_choice;
	wxButton *save_button, *rename_button, *delete_button, *color_button;
	wxStaticText *element_title, *origin_label, *alpha_label, *size_label, *receipt_label, *delay_label;
	wxSpinCtrl* alpha;
	wxSpinCtrlDouble* width;
	wxChoice* receipt;
	wxSpinCtrl* delay;
	std::unique_ptr<PersistLocation> persist;
	bool loading = false;
	bool valid_presets = true;

	void Mark(ChatStyleTag tag, bool refresh_controls = true) {
		changed.set(static_cast<size_t>(tag));
		state.origin[static_cast<size_t>(tag)] = ChatValueOrigin::Edited;
		RefreshAutomaticChatHeader(state.appearance);
		preview->Refresh();
		if (refresh_controls) RefreshInspector();
		else origin_label->SetLabel(_("Edited"));
	}
	void RefreshInspector() {
		loading = true;
		element_title->SetLabel(Labels()[static_cast<size_t>(selected)]);
		auto color = ColorFor(state.appearance, selected);
		bool has_alpha = color && selected != Element::Header && selected != Element::Title;
		auto size = SizeFor(state.appearance, selected);
		color_button->Show(color != nullptr); alpha->Show(has_alpha); alpha_label->Show(has_alpha);
		width->Show(size != nullptr); size_label->Show(size != nullptr);
		bool read = selected == Element::Receipt;
		receipt->Show(read); receipt_label->Show(read); delay->Show(read); delay_label->Show(read);
		if (color) {
			// The existing picker supplies the actual color editor. The swatch
			// uses explicit RGB text as well, so the chosen value is inspectable.
			color_button->SetLabel(_("Color...") + wxS("  ") + to_wx(color->GetAssOverrideFormatted()));
			color_button->SetBackgroundColour(wxColour(color->r, color->g, color->b));
			color_button->SetForegroundColour((color->r * 299 + color->g * 587 + color->b * 114) / 1000 < 128 ? *wxWHITE : *wxBLACK);
			alpha->SetValue(color->a);
		}
		if (size) width->SetValue(*size);
		receipt->SetSelection(state.appearance.read_mark); delay->SetValue(state.appearance.read_time);
		auto origin = state.origin[static_cast<size_t>(TagFor(selected))];
		if (selected == Element::Panel) origin = std::max(origin, state.origin[static_cast<size_t>(ChatStyleTag::PanelAlpha)]);
		if (selected == Element::Receipt) origin = std::max(origin, state.origin[static_cast<size_t>(ChatStyleTag::ReadTime)]);
		origin_label->SetLabel(origin == ChatValueOrigin::Default ? _("Renderer/style default") :
			origin == ChatValueOrigin::Line ? _("Loaded from line") : _("Edited"));
		Layout(); loading = false;
	}
	std::string SelectedPreset() const { return from_wx(preset_choice->GetStringSelection()); }
	void RefreshPresets(std::string const& name = {}) {
		preset_choice->Clear();
		for (auto const& entry : presets.Entries()) preset_choice->Append(to_wx(entry.first));
		preset_choice->SetStringSelection(to_wx(name));
		bool selected_preset = presets.Find(SelectedPreset()) != nullptr;
		save_button->Enable(selected_preset); rename_button->Enable(selected_preset); delete_button->Enable(selected_preset);
	}
	void PersistPresets(std::string const& name = {}) {
		OPT_SET("Tool/Mangetsu Chat/Presets")->SetString(presets.Serialize());
		config::opt->Flush(); RefreshPresets(name);
	}
	void NamePreset(bool rename) {
		if (!valid_presets) { wxMessageBox(_("Saved chat presets could not be read. Repair the user configuration before saving new presets."), GetTitle(), wxOK | wxICON_ERROR, this); return; }
		auto old = SelectedPreset();
		wxTextEntryDialog prompt(this, _("Preset name:"), rename ? _("Rename preset") : _("Save chat style as preset"), rename ? to_wx(old) : wxString());
		if (prompt.ShowModal() != wxID_OK) return;
		auto name = from_wx(prompt.GetValue().Strip(wxString::both));
		bool saved = rename ? presets.Rename(old, name) : presets.Add(name, state.appearance);
		if (!saved) { wxMessageBox(_("Choose a non-empty name which is not already used."), GetTitle(), wxOK | wxICON_INFORMATION, this); return; }
		PersistPresets(name);
	}
	void Apply() {
		auto line = context->selectionController->GetActiveLine();
		if (!line || changed.none()) return;
		auto const original = line->Text.get();
		auto edit = ApplyChatStyle(original, state.appearance, changed);
		if (edit.text != original) {
			int start = 0, end = 0;
			bool restore = context->subsEditBox && context->textSelectionController &&
				context->subsEditBox->MapDisplayRangeToRaw(context->textSelectionController->GetSelectionStart(),
					context->textSelectionController->GetSelectionEnd(), original, start, end);
			if (restore) { start = MapChatTextPosition(start, edit); end = MapChatTextPosition(end, edit); }
			line->Text = edit.text;
			context->ass->Commit(_("set Mangetsu chat style"), AssFile::COMMIT_DIAG_TEXT, -1, line);
			if (restore) context->subsEditBox->SetTextSelection(context->subsEditBox->MapRawToDisplay(start, edit.text),
				context->subsEditBox->MapRawToDisplay(end, edit.text));
		}
		state = LoadChatStyle(line->Text.get(), defaults); changed.reset(); RefreshInspector(); preview->Refresh();
	}
public:
	explicit ChatStyleDialog(agi::Context* context)
	: wxDialog(context->parent, wxID_ANY, _("Mangetsu Chat Style Editor"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), context(context) {
		auto line = context->selectionController->GetActiveLine();
		auto style = context->ass->GetStyle(line->Style);
		defaults = style ? DefaultChatAppearance(style->primary, style->secondary, style->outline, style->shadow, style->outline_w) : DefaultChatAppearance();
		state = LoadChatStyle(line->Text.get(), defaults);
		valid_presets = presets.Load(OPT_GET("Tool/Mangetsu Chat/Presets")->GetString());
		auto root = new wxBoxSizer(wxVERTICAL);
		auto preset_row = new wxBoxSizer(wxHORIZONTAL);
		preset_row->Add(new wxStaticText(this, wxID_ANY, _("Preset:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 6));
		preset_choice = new wxChoice(this, wxID_ANY); preset_row->Add(preset_choice, 1, wxRIGHT, DialogPixels(this, 6));
		auto add_button = [&](wxString const& label) { auto button = new wxButton(this, wxID_ANY, label); preset_row->Add(button, 0, wxRIGHT, DialogPixels(this, 4)); return button; };
		auto new_button = add_button(_("New / Save As...")); save_button = add_button(_("Save"));
		rename_button = add_button(_("Rename...")); delete_button = add_button(_("Delete"));
		root->Add(preset_row, 0, wxEXPAND | wxALL, DialogPixels(this, 10));
		root->Add(new wxStaticText(this, wxID_ANY, _("Select a callout to edit that part of the phone. Alpha: 0 = opaque, 255 = transparent.")), 0, wxLEFT | wxRIGHT, DialogPixels(this, 12));
		preview = new ChatPreview(this, state.appearance, [this](Element element) { selected = element; RefreshInspector(); });
		root->Add(preview, 1, wxEXPAND | wxALL, DialogPixels(this, 4));
		auto inspector = new wxBoxSizer(wxHORIZONTAL);
		element_title = new wxStaticText(this, wxID_ANY, wxString()); inspector->Add(element_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 10));
		color_button = new wxButton(this, wxID_ANY, _("Color...")); inspector->Add(color_button, 0, wxRIGHT, DialogPixels(this, 12));
		alpha_label = new wxStaticText(this, wxID_ANY, _("Alpha:")); inspector->Add(alpha_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 4));
		alpha = new wxSpinCtrl(this, wxID_ANY, wxString(), wxDefaultPosition, DialogPixels(this, wxSize(80, -1)), wxSP_ARROW_KEYS, 0, 255);
		inspector->Add(alpha, 0, wxRIGHT, DialogPixels(this, 12));
		size_label = new wxStaticText(this, wxID_ANY, _("Width:")); inspector->Add(size_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 4));
		width = new wxSpinCtrlDouble(this, wxID_ANY, wxString(), wxDefaultPosition, DialogPixels(this, wxSize(100, -1)), wxSP_ARROW_KEYS, 0, 10000, 0, .25);
		width->SetDigits(3); inspector->Add(width, 0, wxRIGHT, DialogPixels(this, 12));
		width->SetToolTip(_("Width in ASS script coordinates. The preview uses representative geometry."));
		receipt_label = new wxStaticText(this, wxID_ANY, _("Read mark:")); inspector->Add(receipt_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 4));
		receipt = new wxChoice(this, wxID_ANY); receipt->Append(_("None")); receipt->Append(_("Sent")); receipt->Append(_("Read"));
		inspector->Add(receipt, 0, wxRIGHT, DialogPixels(this, 12));
		delay_label = new wxStaticText(this, wxID_ANY, _("Read delay (ms):")); inspector->Add(delay_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DialogPixels(this, 4));
		delay = new wxSpinCtrl(this, wxID_ANY, wxString(), wxDefaultPosition, DialogPixels(this, wxSize(135, -1)), wxSP_ARROW_KEYS, 0, std::numeric_limits<int>::max()); inspector->Add(delay, 0);
		root->Add(inspector, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, DialogPixels(this, 12));
		auto footer = new wxBoxSizer(wxHORIZONTAL);
		origin_label = new wxStaticText(this, wxID_ANY, wxString()); footer->Add(origin_label, 1, wxALIGN_CENTER_VERTICAL);
		auto swap = new wxButton(this, wxID_ANY, _("Swap Left / Right")); footer->Add(swap, 0, wxRIGHT, DialogPixels(this, 6));
		auto reset = new wxButton(this, wxID_ANY, _("Reset selected")); footer->Add(reset, 0, wxRIGHT, DialogPixels(this, 6));
		auto reset_all = new wxButton(this, wxID_ANY, _("Reset All")); footer->Add(reset_all, 0);
		root->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, DialogPixels(this, 12));
		wxString status = state.has_chat_mode ? _("Preview is illustrative. Apply changes the active line only.") :
			_("Current line has no \\chatmode tag; style tags can still be prepared.");
		if (!valid_presets) status += wxS("  ") + _("Saved presets could not be read.");
		root->Add(new wxStaticText(this, wxID_ANY, status), 0, wxLEFT | wxRIGHT | wxBOTTOM, DialogPixels(this, 12));
		root->Add(CreateSeparatedButtonSizer(wxOK | wxAPPLY | wxCANCEL), 0, wxEXPAND | wxALL, DialogPixels(this, 10));
		SetSizer(root);
		RefreshPresets(); RefreshInspector();
		SetMinSize(root->CalcMin() + (GetSize() - GetClientSize()));
		SetSize(DialogPixels(this, wxSize(970, 750)));
		persist = std::make_unique<PersistLocation>(this, "Tool/Mangetsu Chat", true);

		color_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			auto color = ColorFor(state.appearance, selected); if (!color) return;
			auto original = *color, picked = original;
			bool automatic_title = state.appearance.automatic_title, automatic_header = state.appearance.automatic_header;
			// Use Aegisub's picker without touching subtitle text, and only
			// publish its result after acceptance (its callback also runs live).
			bool accepted = GetColorFromUser(this, original, false, [&](agi::Color value) {
				picked = value; picked.a = original.a; *color = picked;
				if (selected == Element::Header) state.appearance.automatic_header = false;
				if (selected == Element::Title) state.appearance.automatic_title = false;
				RefreshAutomaticChatHeader(state.appearance); preview->Refresh();
			});
			if (!accepted) {
				*color = original;
				state.appearance.automatic_title = automatic_title; state.appearance.automatic_header = automatic_header;
				RefreshAutomaticChatHeader(state.appearance); preview->Refresh(); return;
			}
			picked.a = original.a; *color = picked;
			if (selected == Element::Header) state.appearance.automatic_header = false;
			if (selected == Element::Title) state.appearance.automatic_title = false;
			Mark(TagFor(selected));
		});
		auto alpha_changed = [this] {
			if (loading) return;
			if (auto color = ColorFor(state.appearance, selected)) {
				color->a = static_cast<unsigned char>(alpha->GetValue());
				Mark(selected == Element::Panel ? ChatStyleTag::PanelAlpha : TagFor(selected), false);
			}
		};
		alpha->Bind(wxEVT_SPINCTRL, [alpha_changed](wxSpinEvent&) { alpha_changed(); });
		alpha->Bind(wxEVT_TEXT, [alpha_changed](wxCommandEvent&) { alpha_changed(); });
		auto size_changed = [this] { if (!loading) if (auto size = SizeFor(state.appearance, selected)) { *size = width->GetValue(); Mark(TagFor(selected), false); } };
		width->Bind(wxEVT_SPINCTRLDOUBLE, [size_changed](wxSpinDoubleEvent&) { size_changed(); });
		width->Bind(wxEVT_TEXT, [size_changed](wxCommandEvent&) { size_changed(); });
		receipt->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { if (!loading) { state.appearance.read_mark = receipt->GetSelection(); Mark(ChatStyleTag::ReadMark, false); } });
		auto delay_changed = [this] { if (!loading) { state.appearance.read_time = delay->GetValue(); Mark(ChatStyleTag::ReadTime, false); } };
		delay->Bind(wxEVT_SPINCTRL, [delay_changed](wxSpinEvent&) { delay_changed(); });
		delay->Bind(wxEVT_TEXT, [delay_changed](wxCommandEvent&) { delay_changed(); });
		preset_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
			auto name = SelectedPreset(); if (auto preset = presets.Find(name)) {
				state.appearance = *preset; changed.set(); state.origin.fill(ChatValueOrigin::Edited);
				RefreshInspector(); preview->Refresh(); RefreshPresets(name);
			}
		});
		new_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { NamePreset(false); });
		rename_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { NamePreset(true); });
		save_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { auto name = SelectedPreset(); if (presets.Save(name, state.appearance)) PersistPresets(name); });
		delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (presets.Delete(SelectedPreset())) PersistPresets(); });
		swap->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SwapChatSides(state.appearance); Mark(ChatStyleTag::Left); Mark(ChatStyleTag::Right); });
		reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if (auto color = ColorFor(state.appearance, selected)) {
				*color = *ColorFor(defaults, selected);
				if (selected == Element::Panel) Mark(ChatStyleTag::PanelAlpha);
				if (selected == Element::Header) state.appearance.automatic_header = true;
				if (selected == Element::Title) state.appearance.automatic_title = true;
				if (auto size = SizeFor(state.appearance, selected)) *size = *SizeFor(defaults, selected);
			}
			else { state.appearance.read_mark = 2; state.appearance.read_time = 0; Mark(ChatStyleTag::ReadTime); }
			Mark(TagFor(selected));
		});
		reset_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { state.appearance = defaults; changed.set(); state.origin.fill(ChatValueOrigin::Edited); RefreshInspector(); preview->Refresh(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Apply(); }, wxID_APPLY);
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Apply(); EndModal(wxID_OK); }, wxID_OK);
	}
};
} // namespace

void ShowMangetsuChatStyleDialog(agi::Context* context) {
	if (!context || !context->selectionController || !context->selectionController->GetActiveLine()) return;
	ChatStyleDialog dialog(context);
	dialog.ShowModal();
}
