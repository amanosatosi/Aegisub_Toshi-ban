// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include <main.h>

#include "better_view.h"

namespace {
std::string const marker = "[toshiban motion track helper]";

std::string motion_block(std::string const& payload) {
	return "{" + marker + payload + "}";
}
}

TEST(BetterView, CollapsesMarkedMotionTrackingBlock) {
	std::string raw = "abc" + motion_block("\\pos(1,2)\\t(0,40,\\pos(3,4))") + "def";
	auto conversion = agi::BuildBetterViewConversion(raw, true);

	EXPECT_EQ("abc{motion tracking}def", conversion.display_text);
	ASSERT_EQ(1u, conversion.motion_blocks.size());
	EXPECT_TRUE(conversion.motion_blocks[0].collapsed);
}

TEST(BetterView, MotionShorthandComposesWithExistingVisualLineBreaks) {
	std::string raw = "a\\N" + motion_block("\\pos(1,2)") + "\\Nb";
	auto conversion = agi::BuildBetterViewConversion(raw, true);

	EXPECT_EQ("a\n{motion tracking}\nb", conversion.display_text);
	EXPECT_EQ(raw, agi::ApplyBetterViewEdit(conversion, conversion.display_text).raw_text);
}

TEST(BetterView, DisabledLeavesCompleteRawTextVisible) {
	std::string raw = "abc" + motion_block("\\pos(1,2)\\t(0,40,\\pos(3,4))") + "def";
	auto conversion = agi::BuildBetterViewConversion(raw, false);

	EXPECT_EQ(raw, conversion.display_text);
	EXPECT_TRUE(conversion.motion_blocks.empty());
}

TEST(BetterView, MotionBlockCopyIncludesBracesAndCompletePayload) {
	std::string block = motion_block("\\pos(1,2)\\t(0,40,\\pos(3,4))");
	auto conversion = agi::BuildBetterViewConversion("abc" + block + "def", true);

	ASSERT_EQ(1u, conversion.motion_blocks.size());
	EXPECT_EQ(block, conversion.MotionBlockText(conversion.motion_blocks[0]));
}

TEST(BetterView, OrdinaryOverrideAndNonInitialMarkerAreNotCollapsed) {
	std::string ordinary = "abc{\\pos(1,2)\\t(0,40,\\pos(3,4))}def";
	std::string non_initial = "abc{\\pos(1,2)" + marker + "\\t(0,40,\\pos(3,4))}def";
	std::string incomplete = "abc{" + marker + "\\pos(1,2)";

	EXPECT_EQ(ordinary, agi::BuildBetterViewConversion(ordinary, true).display_text);
	EXPECT_EQ(non_initial, agi::BuildBetterViewConversion(non_initial, true).display_text);
	EXPECT_EQ(incomplete, agi::BuildBetterViewConversion(incomplete, true).display_text);
}

TEST(BetterView, LongMotionPayloadRemainsOneMappedDisplaySpan) {
	std::string payload = "\\pos(1,2)";
	for (int i = 0; i < 5000; ++i)
		payload += "\\t(0,40,\\pos(~+3,~-4))";
	std::string block = motion_block(payload);
	auto conversion = agi::BuildBetterViewConversion("a" + block + "z", true);

	EXPECT_EQ("a{motion tracking}z", conversion.display_text);
	ASSERT_EQ(1u, conversion.motion_blocks.size());
	EXPECT_EQ(block, conversion.MotionBlockText(conversion.motion_blocks[0]));
	EXPECT_EQ(1u, conversion.motion_blocks[0].display_begin);
	EXPECT_EQ(1u + std::string("{motion tracking}").size(), conversion.motion_blocks[0].display_end);
}

TEST(BetterView, SurroundingTextAndAtomicBlockRangesMapToRawSource) {
	std::string block = motion_block("\\pos(1,2)");
	std::string raw = "abc" + block + "def";
	auto conversion = agi::BuildBetterViewConversion(raw, true);
	auto const& span = conversion.motion_blocks[0];

	int raw_start = -1;
	int raw_end = -1;
	EXPECT_TRUE(conversion.MapDisplayRangeToRaw(0, 3, raw_start, raw_end));
	EXPECT_EQ(0, raw_start);
	EXPECT_EQ(3, raw_end);
	EXPECT_TRUE(conversion.MapDisplayRangeToRaw(
		static_cast<int>(span.display_begin), static_cast<int>(span.display_end), raw_start, raw_end));
	EXPECT_EQ(3, raw_start);
	EXPECT_EQ(static_cast<int>(3 + block.size()), raw_end);
	EXPECT_EQ(static_cast<int>(span.display_begin), conversion.MapRawToDisplay(static_cast<int>(span.raw_begin)));
	EXPECT_EQ(static_cast<int>(span.display_end), conversion.MapRawToDisplay(static_cast<int>(span.raw_end)));
	EXPECT_TRUE(conversion.MapDisplayRangeToRaw(
		static_cast<int>(span.display_begin + 1), static_cast<int>(span.display_begin + 1), raw_start, raw_end));
	EXPECT_EQ(static_cast<int>(span.raw_begin), raw_start);
	EXPECT_EQ(raw_start, raw_end);
	EXPECT_TRUE(conversion.MapDisplayRangeToRaw(
		static_cast<int>(span.display_end - 1), static_cast<int>(span.display_end - 1), raw_start, raw_end));
	EXPECT_EQ(static_cast<int>(span.raw_end), raw_start);
	EXPECT_EQ(raw_start, raw_end);

	auto edited = agi::ApplyBetterViewEdit(conversion, "abc{motion tracking}XYZ");
	EXPECT_EQ("abc" + block + "XYZ", edited.raw_text);
}

TEST(BetterView, EditingAnyPartOfCollapsedTokenReplacesWholeRawBlock) {
	std::string block = motion_block("\\pos(1,2)");
	auto conversion = agi::BuildBetterViewConversion("abc" + block + "def", true);

	auto deleted = agi::ApplyBetterViewEdit(conversion, "abcdef");
	EXPECT_EQ("abcdef", deleted.raw_text);
}

TEST(BetterView, CaretInsideCollapsedTokenInsertsOnlyBesideRawBlock) {
	std::string block = motion_block("\\pos(1,2)");
	auto conversion = agi::BuildBetterViewConversion("abc" + block + "def", true);
	auto const& span = conversion.motion_blocks[0];

	std::string before_display = conversion.display_text;
	before_display.insert(span.display_begin + 1, "X");
	EXPECT_EQ("abcX" + block + "def", agi::ApplyBetterViewEdit(conversion, before_display).raw_text);

	std::string after_display = conversion.display_text;
	after_display.insert(span.display_end - 1, "Y");
	EXPECT_EQ("abc" + block + "Ydef", agi::ApplyBetterViewEdit(conversion, after_display).raw_text);
}

TEST(BetterView, MultipleMarkedBlocksMapAndCopyIndependently) {
	std::string first = motion_block("\\pos(1,2)");
	std::string second = motion_block("\\scale100");
	auto conversion = agi::BuildBetterViewConversion(first + "x" + second, true);

	EXPECT_EQ("{motion tracking}x{motion tracking}", conversion.display_text);
	ASSERT_EQ(2u, conversion.motion_blocks.size());
	EXPECT_EQ(first, conversion.MotionBlockText(conversion.motion_blocks[0]));
	EXPECT_EQ(second, conversion.MotionBlockText(conversion.motion_blocks[1]));
	EXPECT_NE(conversion.motion_blocks[0].raw_begin, conversion.motion_blocks[1].raw_begin);
	auto clicked_second = conversion.MotionBlockAtDisplayPosition(
		static_cast<int>(conversion.motion_blocks[1].display_begin));
	ASSERT_NE(nullptr, clicked_second);
	EXPECT_EQ(second, conversion.MotionBlockText(*clicked_second));
}

TEST(BetterView, ExpandedBlockIsRawEditableAndCanCollapseAgain) {
	std::string block = motion_block("\\pos(1,2)");
	std::string raw = "abc" + block + "def";
	auto expanded = agi::BuildBetterViewConversion(raw, true, {3});

	EXPECT_EQ(raw, expanded.display_text);
	ASSERT_EQ(1u, expanded.motion_blocks.size());
	EXPECT_FALSE(expanded.motion_blocks[0].collapsed);

	std::string edited_display = raw;
	edited_display.replace(edited_display.find("1,2"), 3, "5,6");
	auto edited = agi::ApplyBetterViewEdit(expanded, edited_display);
	EXPECT_EQ("abc" + motion_block("\\pos(5,6)") + "def", edited.raw_text);
	EXPECT_EQ("abc{motion tracking}def", agi::BuildBetterViewConversion(edited.raw_text, true).display_text);
}

TEST(BetterView, RefreshReflectsRemovalAndUndoReadditionWithoutStaleTokens) {
	std::string block = motion_block("\\pos(1,2)");
	std::string with_block = "abc" + block + "def";
	std::string without_block = "abcdef";

	EXPECT_EQ("abc{motion tracking}def", agi::BuildBetterViewConversion(with_block, true).display_text);
	EXPECT_EQ(without_block, agi::BuildBetterViewConversion(without_block, true).display_text);
	EXPECT_EQ("abc{motion tracking}def", agi::BuildBetterViewConversion(with_block, true).display_text);
}
