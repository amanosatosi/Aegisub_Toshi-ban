#pragma once

#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/checkbox.h>
#include <wx/panel.h>
#include <functional>

class wxStaticText;

namespace mangetsu {
struct Palette {
	wxColour surface{48, 52, 59};
	wxColour secondary{59, 65, 73};
	wxColour border{89, 97, 108};
	wxColour text{240, 242, 244};
	wxColour muted{190, 197, 206};
	wxColour accent{242, 212, 109};
	wxColour hover{255, 228, 138};
	wxColour pressed{217, 185, 79};
	wxColour ink{37, 40, 46};
};

Palette const& Colours();
bool UseBrandColours();
void StyleSurface(wxWindow *window);
void StyleLabels(wxWindow *window);
wxString CompatibilityText();
template<class T> T Dip(wxWindow const* window, T value) {
#if wxCHECK_VERSION(3, 1, 0)
	return window->FromDIP(value);
#else
	(void)window;
	return value;
#endif
}

// The HWND is still a real BUTTON. Windows and wxWidgets handle keyboard
// input, accessibility, focus, capture and wxEVT_BUTTON as usual.
class MangetsuButton final : public wxButton {
	bool selected = true;
	void UpdateMinimumSize();
#ifdef __WXMSW__
	bool hovered = false;
	bool MSWOnDraw(WXDRAWITEMSTRUCT *item) override;
#endif
public:
	MangetsuButton(wxWindow *parent, wxString const& label, wxBitmap const& icon = wxNullBitmap);
	void SetPaintedLabel(wxString const& value);
	void SetSelected(bool value);
private:
	wxString painted_label;
	wxString accessible_position;
	wxBitmap icon;
};

class AlignmentPicker final : public wxPanel {
	MangetsuButton *cells[9]{};
	wxCheckBox *inherit;
	int alignment;
	int inherited;
	void RefreshSelection();
public:
	// Zero means inherit. The event callback receives zero or an ASS numpad
	// value after a user selection.
	AlignmentPicker(wxWindow *parent, int value, int fallback, wxString const& inherit_label,
		std::function<void(int)> changed);
	int Value() const;
};
}
