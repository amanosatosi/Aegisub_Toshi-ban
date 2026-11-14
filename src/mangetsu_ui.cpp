#include "mangetsu_ui.h"

#include <algorithm>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/intl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#ifdef __WXMSW__
#include <wx/msw/dc.h>
#include <wx/msw/wrapwin.h>
#endif

namespace mangetsu {
Palette const& Colours() {
	static Palette const palette;
	return palette;
}

bool UseBrandColours() {
#ifdef __WXMSW__
	HIGHCONTRASTW contrast{};
	contrast.cbSize = sizeof(contrast);
	return !SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
		|| !(contrast.dwFlags & HCF_HIGHCONTRASTON);
#else
	return false;
#endif
}

void StyleSurface(wxWindow *window) {
	if (UseBrandColours()) {
		window->SetBackgroundColour(Colours().surface);
		window->SetForegroundColour(Colours().text);
	}
}

void StyleLabels(wxWindow *window) {
	if (!UseBrandColours()) return;
	for (auto child : window->GetChildren()) {
		if (auto label = wxDynamicCast(child, wxStaticText))
			label->SetForegroundColour(Colours().text);
		if (auto panel = wxDynamicCast(child, wxPanel)) {
			StyleSurface(panel);
			StyleLabels(panel);
		}
	}
}

wxString CompatibilityText() {
	return _("Mangetsu extension. Other ASS renderers may display it differently.");
}

MangetsuButton::MangetsuButton(wxWindow *parent, wxString const& label)
: wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT) {
	SetToolTip(CompatibilityText());
#ifdef __WXMSW__
	if (UseBrandColours()) {
		auto hwnd = reinterpret_cast<HWND>(GetHandle());
		auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
		SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
		Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& evt) { hovered = true; Refresh(false); evt.Skip(); });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& evt) { hovered = false; Refresh(false); evt.Skip(); });
	}
#endif
	UpdateMinimumSize();
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 1, 3)
	Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
		CallAfter([this] { UpdateMinimumSize(); GetParent()->Layout(); });
		event.Skip();
	});
#endif
}

void MangetsuButton::SetSelected(bool value) {
	selected = value;
	if (!accessible_position.empty() && UseBrandColours())
		SetLabel(value ? accessible_position + " (" + _("selected") + ")" : accessible_position);
	Refresh(false);
}

void MangetsuButton::SetPaintedLabel(wxString const& value) {
	painted_label = value;
	accessible_position = GetLabel();
	UpdateMinimumSize();
}

void MangetsuButton::UpdateMinimumSize() {
	auto extent = GetTextExtent(painted_label.empty() ? GetLabelText() : painted_label);
	SetMinSize(wxSize(std::max(extent.x + Dip(this, 24), Dip(this, 38)),
		std::max(extent.y + Dip(this, 10), Dip(this, 32))));
}

#ifdef __WXMSW__
bool MangetsuButton::MSWOnDraw(WXDRAWITEMSTRUCT *item) {
	if (!UseBrandColours()) return wxButton::MSWOnDraw(item);
	auto const& draw = *reinterpret_cast<DRAWITEMSTRUCT *>(item);
	wxRect rect(draw.rcItem.left, draw.rcItem.top, draw.rcItem.right - draw.rcItem.left,
		draw.rcItem.bottom - draw.rcItem.top);
	wxDCTemp dc(draw.hDC, rect.GetSize());
	auto const& palette = Colours();
	bool enabled = IsEnabled() && !(draw.itemState & ODS_DISABLED);
	bool down = (draw.itemState & ODS_SELECTED) != 0;
	auto fill = !enabled ? palette.secondary : down ? palette.pressed :
		hovered ? palette.hover : selected ? palette.accent : palette.secondary;
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();
	dc.SetBrush(wxBrush(fill));
	dc.SetPen(wxPen(palette.border));
	rect.Deflate(Dip(this, 1));
	dc.DrawRoundedRectangle(rect, Dip(this, 5));
	dc.SetFont(GetFont());
	dc.SetTextForeground(!enabled ? palette.muted : (selected || hovered || down) ? palette.ink : palette.text);
	dc.DrawLabel(painted_label.empty() ? GetLabelText() : painted_label, rect, wxALIGN_CENTER);
	if (HasFocus() && enabled) {
		rect.Deflate(Dip(this, 3));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(selected || hovered || down ? palette.ink : palette.accent, Dip(this, 1)));
		dc.DrawRoundedRectangle(rect, Dip(this, 3));
	}
	return true;
}
#endif

AlignmentPicker::AlignmentPicker(wxWindow *parent, int value, int fallback, wxString const& inherit_label,
	std::function<void(int)> changed)
: wxPanel(parent), alignment(value ? value : fallback), inherited(fallback) {
	StyleSurface(this);
	auto layout = new wxBoxSizer(wxVERTICAL);
	inherit = new wxCheckBox(this, wxID_ANY, inherit_label);
	inherit->SetValue(value == 0);
	if (UseBrandColours()) inherit->SetForegroundColour(Colours().text);
	layout->Add(inherit, 0, wxBOTTOM, Dip(this, 8));
	auto grid = new wxGridSizer(3, Dip(this, 4), Dip(this, 4));
	wxString positions[] = {_("Bottom left"), _("Bottom center"), _("Bottom right"),
		_("Middle left"), _("Middle center"), _("Middle right"),
		_("Top left"), _("Top center"), _("Top right")};
	for (int position : {7, 8, 9, 4, 5, 6, 1, 2, 3}) {
		auto cell = new MangetsuButton(this, positions[position - 1]);
		cell->SetPaintedLabel(wxString::Format("%d", position));
		cells[position - 1] = cell;
		cell->SetName(positions[position - 1]);
		cell->SetToolTip(positions[position - 1] + "\n" + CompatibilityText());
		cell->Bind(wxEVT_BUTTON, [this, position, changed](wxCommandEvent&) {
			alignment = position;
			inherit->SetValue(false);
			RefreshSelection();
			changed(Value());
		});
		cell->Bind(wxEVT_KEY_DOWN, [this, position](wxKeyEvent& event) {
			int next = position;
			switch (event.GetKeyCode()) {
				case WXK_LEFT: if (position % 3 != 1) --next; break;
				case WXK_RIGHT: if (position % 3 != 0) ++next; break;
				case WXK_UP: if (position <= 6) next += 3; break;
				case WXK_DOWN: if (position >= 4) next -= 3; break;
				default: event.Skip(); return;
			}
			cells[next - 1]->SetFocus();
		});
		grid->Add(cell, 0, wxEXPAND);
	}
	layout->Add(grid, 0);
	inherit->Bind(wxEVT_CHECKBOX, [this, changed](wxCommandEvent&) {
		RefreshSelection();
		changed(Value());
	});
	SetSizer(layout);
	RefreshSelection();
}

int AlignmentPicker::Value() const {
	return inherit->GetValue() ? 0 : alignment;
}

void AlignmentPicker::RefreshSelection() {
	int shown = inherit->GetValue() ? inherited : alignment;
	for (int index = 0; index < 9; ++index) {
		cells[index]->SetSelected(index + 1 == shown);
		if (!UseBrandColours())
			cells[index]->SetLabel(wxString::Format(index + 1 == shown ? "[%d]" : "%d", index + 1));
	}
}
}
