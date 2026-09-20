// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <string>

class AssDialogue;

struct MangetsuCurvedTextState {
	std::string path;
	double along_offset = 0.0;
	double normal_offset = 0.0;
	int alignment = 0;
	bool has_path = false;
	bool has_along_offset = false;
	bool has_normal_offset = false;
	bool has_alignment = false;
};

/// Read the effective static curved-text state before the first renderable run.
/// Tags nested in \t are deliberately not returned.
MangetsuCurvedTextState GetMangetsuCurvedText(AssDialogue const& line);

/// Strictly validate the editable subset of ASS drawings used by the visual
/// tool. Currently this is m/l/b only; unknown commands are left untouched.
bool IsSupportedMangetsuCurvedTextPath(std::string const& path);

/// Update only the effective static tag. These functions preserve unrelated
/// blocks and never edit a \ct tag nested in \t.
bool SetMangetsuCurvedTextPath(AssDialogue& line, std::string const& path);
bool SetMangetsuCurvedTextAlongOffset(AssDialogue& line, double value);
bool SetMangetsuCurvedTextNormalOffset(AssDialogue& line, double value);
bool SetMangetsuCurvedTextAlignment(AssDialogue& line, int value);
