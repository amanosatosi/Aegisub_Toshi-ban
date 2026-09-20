// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include <main.h>

#include "ass_dialogue.h"
#include "mangetsu_curved_text.h"

namespace {
AssDialogue MakeLine(std::string const& text) {
	AssDialogue line;
	line.Text = text;
	return line;
}
}

TEST(MangetsuCurvedText, ParsesBalancedPathAndScalarTags) {
	auto state = GetMangetsuCurvedText(MakeLine(
		"{\\an5\\ct(m -400 100 b -250 -200 250 -200 400 100)\\ctx12.5\\cty-8\\ctan3}TEXT"));
	EXPECT_TRUE(state.has_path);
	EXPECT_EQ("m -400 100 b -250 -200 250 -200 400 100", state.path);
	EXPECT_DOUBLE_EQ(12.5, state.along_offset);
	EXPECT_DOUBLE_EQ(-8.0, state.normal_offset);
	EXPECT_EQ(3, state.alignment);
}

TEST(MangetsuCurvedText, SupportsMoveLineAndCubicButRejectsUnknownCommands) {
	EXPECT_TRUE(IsSupportedMangetsuCurvedTextPath("m -300 0 l 300 0"));
	EXPECT_TRUE(IsSupportedMangetsuCurvedTextPath("m -400 100 b -250 -200 250 -200 400 100"));
	EXPECT_FALSE(IsSupportedMangetsuCurvedTextPath("m 0 0 s 20 20 40 40"));
	EXPECT_FALSE(IsSupportedMangetsuCurvedTextPath("m 0 0 b 1 2 3 4"));
}

TEST(MangetsuCurvedText, PathEditPreservesUnrelatedTagsCommentsAndBurmeseText) {
	auto line = MakeLine("{note}{\\an5\\bord3\\ct(m -10 0 l 10 0)\\c&HFFFFFF&}မြန်မာစာ သင်္ချာ ကြိုဆိုပါတယ်");
	ASSERT_TRUE(SetMangetsuCurvedTextPath(line, "m -20 5 b -10 -5 10 -5 20 5"));
	EXPECT_EQ("{note}{\\an5\\bord3\\ct(m -20 5 b -10 -5 10 -5 20 5)\\c&HFFFFFF&}မြန်မာစာ သင်္ချာ ကြိုဆိုပါတယ်", line.Text.get());
}

TEST(MangetsuCurvedText, StaticEditDoesNotTouchAnimatedPath) {
	auto line = MakeLine("{\\t(0,1000,\\ct(m 0 0 l 100 0))\\bord2}TEXT");
	ASSERT_TRUE(SetMangetsuCurvedTextPath(line, "m -50 0 l 50 0"));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\t(0,1000,\\ct(m 0 0 l 100 0))"));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\ct(m -50 0 l 50 0)"));
}

TEST(MangetsuCurvedText, ScalarAndAlignmentEditsDoNotRewritePath) {
	auto line = MakeLine("{\\ct(m -300 0 l 300 0)\\bord2}TEXT");
	ASSERT_TRUE(SetMangetsuCurvedTextAlongOffset(line, 42.25));
	ASSERT_TRUE(SetMangetsuCurvedTextNormalOffset(line, -13.5));
	ASSERT_TRUE(SetMangetsuCurvedTextAlignment(line, 2));
	EXPECT_EQ("{\\ct(m -300 0 l 300 0)\\bord2\\ctx42.25\\cty-13.5\\ctan2}TEXT", line.Text.get());
}

TEST(MangetsuCurvedText, MalformedEditFailsWithoutChangingLine) {
	auto line = MakeLine("{\\ct(m 0 0 l 100 0)}TEXT");
	auto before = line.Text.get();
	EXPECT_FALSE(SetMangetsuCurvedTextPath(line, "m 0 b broken"));
	EXPECT_EQ(before, line.Text.get());
}
