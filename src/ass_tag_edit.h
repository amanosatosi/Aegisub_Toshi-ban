// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <functional>
#include <string>

namespace agi { namespace ass {

enum class FadeSide {
	In,
	Out
};

enum class FadeColorChoice { Normal, White, Black, Pick };

/// Resolve one visible fade choice, apply it to the selection, then commit once.
/// The picker is called only for Pick; cancellation never applies or commits.
bool RunFadeOperation(FadeColorChoice choice,
	std::function<bool(std::string&)> const& pick_color,
	std::function<bool(std::string const&)> const& apply_selection,
	std::function<void()> const& commit);

struct AlignmentEditResult {
	std::string text;
	int caret = 0;
};

struct FadeEditResult {
	std::string text;
	// Optional removal before the main edit, when moving an existing fad in
	// front of an earlier long-form fade to keep the new operation effective.
	int removal_start = -1;
	int removal_end = -1;
	int edit_start = 0;
	int edit_end = 0;
	int replacement_length = 0;
};

int MoveTextPositionAfterFadeEdit(int position, FadeEditResult const& result);

/// Delete the selected range, set the line's alignment, and return the
/// collapsed caret position mapped through the tag edit.
AlignmentEditResult SetLineAlignment(
	std::string const& text, int alignment, int selection_start, int selection_end);

/// Set one duration/color side of the effective line-level \fad tag.
/// An empty color requests ordinary alpha fading on that side. The opposite
/// side is preserved, including its color and +a modifier.
FadeEditResult SetColoredFade(
	std::string const& text, FadeSide side, int milliseconds, std::string const& color);

/// Get a color argument from the effective line-level \fad, or an empty string
/// when that side uses ordinary alpha fading.
std::string GetFadeColor(std::string const& text, FadeSide side);

/// Calculate a fade duration from one video time and the active line only.
/// Returns -1 when the line has invalid timing; otherwise clamps to the line.
int FadeDurationFromVideoTime(FadeSide side, int video_time, int line_start, int line_end);

/// Map a raw text position through a single replacement.
int MoveTextPositionAfterEdit(
	int position, int edit_start, int edit_end, int replacement_length);

} }
