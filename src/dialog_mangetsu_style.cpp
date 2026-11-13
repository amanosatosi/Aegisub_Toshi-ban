// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "dialog_mangetsu_style.h"
#include "mangetsu_style_metadata.h"
#include "ass_style.h"
#include "colour_button.h"
#include "compat.h"
#include "format.h"
#include <libaegisub/color.h>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <wx/button.h>
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
	wxButton *reset = nullptr;
	wxWindow *input = nullptr;
	wxTextCtrl *first = nullptr, *second = nullptr;
	ColourButton *color = nullptr;
	wxChoice *choice = nullptr;
	std::string original;
	wxString original_first, original_second;
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
			{n + "bc", _("Border color"), Kind::Color}, {n + "ba", _(L"Border alpha (0\u2013255)"), Kind::Alpha},
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
	bool Save(Row const *reset = nullptr) {
		LinkAxes();
		auto candidate = working;
		// With the axes linked, the X reset applies to the pair.
		auto reset_y = linked && linked->GetValue() && reset == rows[AxisX()].get()
			? rows[AxisX() + 1].get() : nullptr;
		for (auto const& entry : rows) {
			auto& row = *entry;
			if (&row == reset || &row == reset_y) {
				if (row.originally_assigned)
					candidate.Set(row.field.key, std::nullopt);
				continue;
			}
			if (row.first && row.first->GetValue() == row.original_first &&
				(!row.second || row.second->GetValue() == row.original_second))
				continue;
			std::string value;
			try { value = Read(row, true); }
			catch (std::invalid_argument const&) {
				wxMessageBox(fmt_tl("Invalid value for %s.", row.field.label) + "\n\n" +
					_("Enter a finite number in the allowed range. Sizes and scales cannot be negative; alpha must be an integer from 0 to 255. Blur cannot exceed 100; edge blur cannot exceed 127."),
					_("Invalid style value"), wxOK | wxICON_ERROR, this);
				(row.first ? row.first : row.input)->SetFocus();
				return false;
			}
			if (value != row.original)
				candidate.Set(row.field.key, value);
		}
		working = std::move(candidate);
		return true;
	}
	size_t AxisX() const { return page == StyleEditorPage::Furigana ? 1 : 0; }
	void LinkAxes() {
		if (!linked) return;
		auto& first = *rows[AxisX()];
		auto& second = *rows[AxisX() + 1];
		if (linked->GetValue())
			second.first->ChangeValue(first.first->GetValue());
		second.input->Enable(!linked->GetValue());
		second.reset->Enable(!linked->GetValue());
	}
	void LoadValues(StyleValues const& values) {
		for (auto const& row : rows) {
			auto const& value = values.at(row->field.key);
			SetInput(*row, value.value);
			row->originally_assigned = value.assigned;
			row->original = Read(*row, false);
			if (row->first) row->original_first = row->first->GetValue();
			if (row->second) row->original_second = row->second->GetValue();
		}
	}
	void Build() {
		linked = nullptr;
		rows.clear();
		body->Clear(true);
		auto form = new wxFlexGridSizer(3, Dip(5), Dip(8));
		form->AddGrowableCol(1);
		for (auto field : Fields()) {
			auto row = std::make_unique<Row>();
			row->field = field;
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
			form->Add(row->input, 1, wxEXPAND);
			row->reset = new wxButton(this, wxID_ANY, _("Reset"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			row->reset->SetToolTip(fmt_tl("Restore the inherited value for %s.", field.label));
			form->Add(row->reset, 0, wxALIGN_CENTER_VERTICAL);
			auto raw = row.get();
			row->reset->Bind(wxEVT_BUTTON, [this, raw](wxCommandEvent&) {
				if (!Save(raw)) return;
				LoadValues(working.Values(style, wrap));
				LinkAxes();
			});
			rows.push_back(std::move(row));
		}
		body->Add(form, 1, wxEXPAND);
		LoadValues(working.Values(style, wrap));
		if (page != StyleEditorPage::Vertical) {
			linked = new wxCheckBox(this, wxID_ANY, page == StyleEditorPage::Furigana ? _("Use the same reading scale on both axes") : _("Use the same border size on both axes"));
			auto x = AxisX();
			linked->SetValue(rows[x]->original == rows[x + 1]->original && rows[x]->originally_assigned == rows[x + 1]->originally_assigned);
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
