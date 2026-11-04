// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once
class wxWindow;
class AssStyle;
namespace mangetsu {
class StyleMetadataDraft;
enum class StyleEditorPage { Furigana, Vertical, Borders };
bool EditStyleProperties(wxWindow *parent, StyleMetadataDraft& pending,
	AssStyle const& style, int script_wrap, StyleEditorPage page);
}
