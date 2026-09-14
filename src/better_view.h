// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace agi {

struct BetterViewMotionBlock {
	size_t raw_begin;
	size_t raw_end;
	size_t display_begin;
	size_t display_end;
	bool collapsed;
};

struct BetterViewConversion {
	std::string raw_text;
	std::string display_text;
	std::vector<int> display_to_raw;
	std::vector<BetterViewMotionBlock> motion_blocks;

	bool MapDisplayRangeToRaw(int display_start, int display_end, int& raw_start, int& raw_end) const;
	int MapRawToDisplay(int raw_offset) const;
	BetterViewMotionBlock const* MotionBlockAtDisplayPosition(int display_pos) const;
	std::string MotionBlockText(BetterViewMotionBlock const& block) const;
};

struct BetterViewEdit {
	std::string raw_text;
	size_t replaced_raw_begin = 0;
	size_t replaced_raw_end = 0;
	size_t inserted_raw_size = 0;
};

BetterViewConversion BuildBetterViewConversion(
	std::string const& raw_text,
	bool enabled,
	std::vector<size_t> const& expanded_motion_blocks = std::vector<size_t>());

std::string BetterViewDisplayToAss(std::string const& display_text);
BetterViewEdit ApplyBetterViewEdit(BetterViewConversion const& previous, std::string const& edited_display_text);

} // namespace agi
