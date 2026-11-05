// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "dialog_mangetsu_style.h"
#include "mangetsu_style_metadata.h"
#include "ass_style.h"
#include "colour_button.h"
#include "compat.h"
#include <libaegisub/color.h>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace mangetsu {
namespace {
enum class Kind { Number, Signed, Pair, Color, Alpha, Choice };
struct Field {
	std::string key;
	wxString label;
	Kind kind = Kind::Number;
	std::vector<wxString> choices;
};
struct Row {
	Field field;
	wxCheckBox *assigned = nullptr;
	wxWindow *input = nullptr;
	wxTextCtrl *first = nullptr, *second = nullptr;
	ColourButton *color = nullptr;
	wxChoice *choice = nullptr;
	std::string original;
	bool originally_assigned = false;
};
class PropertyDialog final : public wxDialog {
	StyleMetadataDraft working;
	AssStyle const& style;
	int wrap;
	StyleEditorPage page;
	int layer = 1;
	wxRadioBox *layers = nullptr;
	wxBoxSizer *body;
	wxCheckBox *linked = nullptr;
	std::vector<std::unique_ptr<Row>> rows;
	int Dip(int value) const {
#if wxCHECK_VERSION(3, 1, 0)
		return FromDIP(value);
#else
		return value;
#endif
	}
	wxString Title() const {
		return page == StyleEditorPage::Furigana ? _("Furigana") : page == StyleEditorPage::Vertical ? _("Vertical layout") : _("Multi-border");
	}
	std::vector<Field> Fields() const {
		auto boolean = std::vector<wxString>{_("Disabled"), _("Enabled")};
		if (page == StyleEditorPage::Furigana) return {
			{"furi", _("Furigana"), Kind::Choice, boolean},
			{"furisx", _("Reading scale X (%)")}, {"furisy", _("Reading scale Y (%)")},
			{"furifsp", _("Reading spacing"), Kind::Signed},
			{"furistyle", _("Reading style"), Kind::Choice, {_("Normal (0)"), _("Compatible normal (1)"), _("Manga X-fit (2)")}},
			{"furipos", _("Reading offset X, Y"), Kind::Pair},
			{"furiap", _("Automatic reading gap"), Kind::Choice, boolean},
			{"furiplaceauto", _("Outward reading placement"), Kind::Choice, boolean},
			{"furichangepos", _("Reading clearance"), Kind::Choice, {_("Compact"), _("Reserve block space"), _("Strict interline")}}};
		if (page == StyleEditorPage::Vertical) return {
			{"vert", _("Native vertical layout"), Kind::Choice, boolean},
			{"vtype", _("Vertical unit policy"), Kind::Choice, {_("Automatic by script"), _("CJK cells"), _("Latin optical"), _("Myanmar optical")}},
			{"vdir", _("Column direction"), Kind::Choice, {_("Automatic by script"), _("Advance left"), _("Advance right")}},
			{"vsp", _("Vertical unit spacing"), Kind::Signed}, {"vcolsp", _("Column spacing"), Kind::Signed}};
		auto n = std::to_string(layer);
		return {{n + "bsx", _("Border size X")}, {n + "bsy", _("Border size Y")},
			{n + "bc", _("Border color"), Kind::Color}, {n + "ba", _("Border alpha (0–255)"), Kind::Alpha},
			{n + "bblur", _("Blur")}, {n + "bbe", _("Edge blur")}, {n + "bsc", _("Waiting karaoke color"), Kind::Color}};
	}
	std::string Read(Row const& row, bool validate) const {
		if (row.choice) return std::to_string(row.choice->GetSelection());
		if (row.color) return row.color->GetColor().GetAssOverrideFormatted();
		double first, second = 0;
		auto number = [](wxTextCtrl *input, double& value) {
			return (input->GetValue().ToDouble(&value) || ParseStyleNumber(from_wx(input->GetValue()), value)) && std::isfinite(value);
		};
		if (!number(row.first, first) ||
			((row.field.kind == Kind::Number || row.field.kind == Kind::Alpha) && first < 0) ||
			(row.field.kind == Kind::Alpha && (first > 255 || first != std::floor(first))) ||
			(row.field.key.find("bblur") != std::string::npos && first > 100) ||
			(row.field.key.find("bbe") != std::string::npos && first > 127) ||
			(row.second && !number(row.second, second))) {
			if (validate) throw std::invalid_argument("number");
			return "";
		}
		if (row.field.kind == Kind::Alpha) {
			std::ostringstream hex;
			hex << "&H" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(first) << '&';
			return hex.str();
		}
		return StyleNumber(first) + (row.second ? ',' + StyleNumber(second) : "");
	}
	void SetInput(Row& row, std::string value) {
		if (row.choice) row.choice->SetSelection(std::stoi(value));
		else if (row.color) row.color->SetColor(agi::Color(value));
		else {
			if (row.field.kind == Kind::Alpha) value = std::to_string(std::stoul(value.substr(2), nullptr, 16));
			auto comma = value.find(',');
			row.first->ChangeValue(to_wx(value.substr(0, comma)));
			if (row.second) row.second->ChangeValue(to_wx(value.substr(comma + 1)));
		}
	}
	bool Save() {
		for (auto const& entry : rows) {
			auto& row = *entry;
			bool assigned = row.assigned->GetValue();
			std::string value;
			try { if (assigned) value = Read(row, true); }
			catch (...) {
				wxMessageBox(_("Enter a finite number in the allowed range. Sizes and scales cannot be negative; alpha must be an integer from 0 to 255. Blur cannot exceed 100; edge blur cannot exceed 127."), _("Invalid style value"), wxOK | wxICON_ERROR, this);
				row.input->SetFocus(); return false;
			}
			if (assigned != row.originally_assigned || (assigned && value != row.original))
				working.Set(row.field.key, assigned ? std::optional<std::string>(value) : std::nullopt);
		}
		return true;
	}
	void LinkAxes() {
		if (!linked || rows.size() < 2) return;
		size_t x = page == StyleEditorPage::Furigana ? 1 : 0;
		auto& first = *rows[x]; auto& second = *rows[x + 1];
		if (linked->GetValue()) {
			second.assigned->SetValue(first.assigned->GetValue());
			second.first->ChangeValue(first.first->GetValue());
		}
		second.assigned->Enable(!linked->GetValue());
		second.input->Enable(!linked->GetValue() && second.assigned->GetValue());
	}
	void Build() {
		rows.clear();
		body->Clear(true);
		auto values = working.Values(style, wrap);
		auto form = new wxFlexGridSizer(3, Dip(5), Dip(8));
		form->AddGrowableCol(2);
		for (auto field : Fields()) {
			auto row = std::make_unique<Row>();
			row->field = field;
			row->assigned = new wxCheckBox(this, wxID_ANY, _("Override"));
			row->assigned->SetToolTip(_("Unchecked uses the style or native default. Checked writes an explicit Mangetsu value."));
			form->Add(row->assigned, 0, wxALIGN_CENTER_VERTICAL);
			form->Add(new wxStaticText(this, wxID_ANY, field.label), 0, wxALIGN_CENTER_VERTICAL);
			if (field.kind == Kind::Choice) row->input = row->choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, static_cast<int>(field.choices.size()), field.choices.data());
			else if (field.kind == Kind::Color) row->input = row->color = new ColourButton(this, GetTextExtent("000000"), false);
			else {
				row->first = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_RIGHT);
				row->first->SetMinSize(wxSize(GetTextExtent("000000.0000").x, -1));
				if (field.kind == Kind::Pair) {
					auto panel = new wxPanel(this);
					row->first->Reparent(panel);
					row->second = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_RIGHT);
					auto pair = new wxBoxSizer(wxHORIZONTAL);
					pair->Add(row->first, 1, wxRIGHT, Dip(4)); pair->Add(row->second, 1);
					panel->SetSizer(pair); row->input = panel;
				} else row->input = row->first;
			}
			SetInput(*row, values.at(field.key).value);
			row->originally_assigned = values.at(field.key).assigned;
			row->assigned->SetValue(row->originally_assigned);
			row->input->Enable(row->originally_assigned);
			row->original = Read(*row, false);
			form->Add(row->input, 1, wxEXPAND);
			auto raw = row.get();
			row->assigned->Bind(wxEVT_CHECKBOX, [this, raw](wxCommandEvent&) {
				if (!raw->assigned->GetValue()) {
					auto reset = working; reset.Set(raw->field.key, std::nullopt);
					SetInput(*raw, reset.Values(style, wrap).at(raw->field.key).value);
				}
				raw->input->Enable(raw->assigned->GetValue()); LinkAxes();
			});
			rows.push_back(std::move(row));
		}
		body->Add(form, 1, wxEXPAND);
		linked = nullptr;
		if (page != StyleEditorPage::Vertical) {
			linked = new wxCheckBox(this, wxID_ANY, page == StyleEditorPage::Furigana ? _("Use the same reading scale on both axes") : _("Use the same border size on both axes"));
			size_t x = page == StyleEditorPage::Furigana ? 1 : 0;
			linked->SetValue(Read(*rows[x], false) == Read(*rows[x + 1], false) && rows[x]->originally_assigned == rows[x + 1]->originally_assigned);
			linked->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { LinkAxes(); });
			rows[x]->first->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { LinkAxes(); });
			body->Add(linked, 0, wxTOP, Dip(8));
			LinkAxes();
		}
		Layout();
	}
public:
	PropertyDialog(wxWindow *parent, StyleMetadataDraft const& draft, AssStyle const& style, int wrap, StyleEditorPage page)
	: wxDialog(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), working(draft), style(style), wrap(wrap), page(page) {
		SetTitle(Title());
		auto main = new wxBoxSizer(wxVERTICAL);
		if (page == StyleEditorPage::Borders) {
			wxString numbers[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
			layers = new wxRadioBox(this, wxID_ANY, _("Border layer"), wxDefaultPosition, wxDefaultSize, 10, numbers, 10, wxRA_SPECIFY_COLS);
			layers->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) {
				if (!Save()) { layers->SetSelection(layer - 1); return; }
				layer = layers->GetSelection() + 1; Build();
			});
			main->Add(layers, 0, wxALL | wxEXPAND, Dip(10));
		}
		body = new wxBoxSizer(wxVERTICAL);
		main->Add(body, 1, wxALL | wxEXPAND, Dip(10));
		auto hint = new wxStaticText(this, wxID_ANY, page == StyleEditorPage::Borders
			? _("Color alone does not enable a layer. Manual gradients remain in effect; these colors are solid fallbacks.")
			: _("Defaults apply before dialogue overrides. Changes remain pending until the Style Editor is applied."));
		hint->Wrap(GetTextExtent("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM").x);
		main->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, Dip(10));
		main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, Dip(10));
		SetSizer(main); Build(); Fit(); SetMinSize(GetSize());
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (Save()) EndModal(wxID_OK); }, wxID_OK);
		CenterOnParent();
	}
	StyleMetadataDraft const& Result() const { return working; }
};
}
bool EditStyleProperties(wxWindow *parent, StyleMetadataDraft& pending, AssStyle const& style, int script_wrap, StyleEditorPage page) {
	PropertyDialog dialog(parent, pending, style, script_wrap, page);
	if (dialog.ShowModal() != wxID_OK) return false;
	pending = dialog.Result(); return true;
}
}
