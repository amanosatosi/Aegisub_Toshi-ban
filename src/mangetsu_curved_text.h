// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "vector2d.h"

#include <string>
#include <vector>

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

enum class MangetsuCurvedTextSegmentType {
	LINE,
	CUBIC
};

struct MangetsuCurvedTextSegment {
	MangetsuCurvedTextSegmentType type = MangetsuCurvedTextSegmentType::LINE;
	Vector2D start = Vector2D(0, 0);
	Vector2D control1 = Vector2D(0, 0);
	Vector2D control2 = Vector2D(0, 0);
	Vector2D end = Vector2D(0, 0);
};

struct MangetsuCurvedTextPath {
	Vector2D start = Vector2D(0, 0);
	std::vector<MangetsuCurvedTextSegment> segments;
};

struct MangetsuCurvedTextArc {
	Vector2D start = Vector2D(0, 0);
	Vector2D bend = Vector2D(0, 0);
	Vector2D end = Vector2D(0, 0);
};

/// Read the event-wide curved-text state. Mangetsu's first valid static path
/// wins and survives style resets; tags nested in \t are deliberately ignored.
MangetsuCurvedTextState GetMangetsuCurvedText(AssDialogue const& line);

/// Strictly validate the editable subset of ASS drawings used by the visual
/// tool. Currently this is m/l/b only; unknown commands are left untouched.
bool IsSupportedMangetsuCurvedTextPath(std::string const& path);

/// Parse/format the lossless m/l/b subset used by the visual authoring tool.
bool ParseMangetsuCurvedTextPath(std::string const& path, MangetsuCurvedTextPath& geometry);
std::string FormatMangetsuCurvedTextPath(MangetsuCurvedTextPath const& geometry);

/// Convert between the default three-handle arc model and ASS path syntax.
/// Only a single line or a cubic which is exactly a degree-elevated quadratic
/// is accepted as a simple arc; other paths must stay in advanced mode.
bool GetMangetsuCurvedTextArc(std::string const& path, MangetsuCurvedTextArc& arc);
std::string FormatMangetsuCurvedTextArc(MangetsuCurvedTextArc const& arc);

/// Pure geometry operations used by toolbar actions and unit tests.
std::string MakeDefaultMangetsuCurvedTextPath(double width, int alignment);
bool ResetMangetsuCurvedTextPathStraight(std::string const& path, std::string& result);
bool ReverseMangetsuCurvedTextPath(std::string const& path, std::string& result);

/// Update only the effective static tag. These functions preserve unrelated
/// blocks and never edit a \ct tag nested in \t.
bool SetMangetsuCurvedTextPath(AssDialogue& line, std::string const& path);
bool RemoveMangetsuCurvedTextPath(AssDialogue& line);
bool SetMangetsuCurvedTextAlongOffset(AssDialogue& line, double value);
bool SetMangetsuCurvedTextNormalOffset(AssDialogue& line, double value);
bool SetMangetsuCurvedTextAlignment(AssDialogue& line, int value);
bool RemoveMangetsuCurvedTextAlignment(AssDialogue& line);
