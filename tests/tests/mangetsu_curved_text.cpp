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

TEST(MangetsuCurvedText, CreatesAlignmentAwareDefaultPathWithoutManualSyntax) {
	auto line = MakeLine("{\\pos(936,565)}testing");
	auto path = MakeDefaultMangetsuCurvedTextPath(200, 5);
	EXPECT_EQ("m -100 0 l 100 0", path);
	ASSERT_TRUE(SetMangetsuCurvedTextPath(line, path));
	EXPECT_EQ("{\\pos(936,565)\\ct(m -100 0 l 100 0)}testing", line.Text.get());
	EXPECT_EQ("m 0 0 l 200 0", MakeDefaultMangetsuCurvedTextPath(200, 4));
	EXPECT_EQ("m -200 0 l 0 0", MakeDefaultMangetsuCurvedTextPath(200, 6));
}

TEST(MangetsuCurvedText, SerializesStraightAndCurvedArcs) {
	MangetsuCurvedTextArc straight {Vector2D(-100, 0), Vector2D(0, 0), Vector2D(100, 0)};
	EXPECT_EQ("m -100 0 l 100 0", FormatMangetsuCurvedTextArc(straight));

	MangetsuCurvedTextArc upward {Vector2D(-100, 0), Vector2D(0, -50), Vector2D(100, 0)};
	EXPECT_EQ("m -100 0 b -33.33 -66.67 33.33 -66.67 100 0", FormatMangetsuCurvedTextArc(upward));

	MangetsuCurvedTextArc downward {Vector2D(-100, 0), Vector2D(0, 50), Vector2D(100, 0)};
	EXPECT_EQ("m -100 0 b -33.33 66.67 33.33 66.67 100 0", FormatMangetsuCurvedTextArc(downward));
}

TEST(MangetsuCurvedText, DiagonalArcRoundTripsItsVisualHandles) {
	MangetsuCurvedTextArc source {Vector2D(-80, 20), Vector2D(15, -35), Vector2D(130, 90)};
	MangetsuCurvedTextArc loaded;
	ASSERT_TRUE(GetMangetsuCurvedTextArc(FormatMangetsuCurvedTextArc(source), loaded));
	EXPECT_NEAR(source.start.X(), loaded.start.X(), .02);
	EXPECT_NEAR(source.start.Y(), loaded.start.Y(), .02);
	EXPECT_NEAR(source.bend.X(), loaded.bend.X(), .02);
	EXPECT_NEAR(source.bend.Y(), loaded.bend.Y(), .02);
	EXPECT_NEAR(source.end.X(), loaded.end.X(), .02);
	EXPECT_NEAR(source.end.Y(), loaded.end.Y(), .02);
}

TEST(MangetsuCurvedText, LoadsToolGeneratedCubicButRejectsLossyArcSimplification) {
	MangetsuCurvedTextArc arc;
	ASSERT_TRUE(GetMangetsuCurvedTextArc(
		"m -100 0 b -33.33 -66.67 33.33 -66.67 100 0", arc));
	EXPECT_NEAR(0, arc.bend.X(), .02);
	EXPECT_NEAR(-50, arc.bend.Y(), .02);
	EXPECT_FALSE(GetMangetsuCurvedTextArc(
		"m 0 0 b 10 80 90 -20 100 0", arc));
}

TEST(MangetsuCurvedText, ResetsAndReversesWithoutChangingPathEndpoints) {
	std::string straight;
	ASSERT_TRUE(ResetMangetsuCurvedTextPathStraight(
		"m 0 0 l 40 20 b 60 30 80 30 100 0", straight));
	EXPECT_EQ("m 0 0 l 100 0", straight);

	std::string reversed;
	ASSERT_TRUE(ReverseMangetsuCurvedTextPath(
		"m 0 0 l 40 20 b 60 30 80 30 100 0", reversed));
	EXPECT_EQ("m 100 0 b 80 30 60 30 40 20 l 0 0", reversed);
}

TEST(MangetsuCurvedText, RemoveCurvePreservesUnrelatedTagsTextAndTransforms) {
	auto line = MakeLine("{\\pos(936,565)\\bord5\\ct(m -100 0 l 100 0)\\blur1\\t(\\ct(m 0 0 l 1 0))}testing");
	ASSERT_TRUE(RemoveMangetsuCurvedTextPath(line));
	EXPECT_EQ("{\\pos(936,565)\\bord5\\blur1\\t(\\ct(m 0 0 l 1 0))}testing", line.Text.get());
	auto only_curve = MakeLine("{\\ct(m -10 0 l 10 0)}testing");
	ASSERT_TRUE(RemoveMangetsuCurvedTextPath(only_curve));
	EXPECT_EQ("testing", only_curve.Text.get());
}

TEST(MangetsuCurvedText, ComplexPathIsPreservedAndUsesAdvancedMode) {
	auto path = "m -100 0 l -40 -20 0 -10 b 25 30 75 30 100 0";
	MangetsuCurvedTextArc arc;
	EXPECT_TRUE(IsSupportedMangetsuCurvedTextPath(path));
	EXPECT_FALSE(GetMangetsuCurvedTextArc(path, arc));
	auto line = MakeLine(std::string("{\\bord2\\ct(") + path + ")}မြန်မာစာ");
	auto before = line.Text.get();
	EXPECT_EQ(path, GetMangetsuCurvedText(line).path);
	EXPECT_EQ(before, line.Text.get());
}

TEST(MangetsuCurvedText, FirstValidStaticPathWinsAcrossResets) {
	auto line = MakeLine(
		"{\\ct(l 0 0)\\ct(m -50 0 l 50 0)\\r\\ct(m -10 0 l 10 0)}TEXT");
	EXPECT_EQ("m -50 0 l 50 0", GetMangetsuCurvedText(line).path);
	ASSERT_TRUE(SetMangetsuCurvedTextPath(line, "m -75 0 l 75 0"));
	EXPECT_EQ("{\\ct(l 0 0)\\ct(m -75 0 l 75 0)\\r\\ct(m -10 0 l 10 0)}TEXT", line.Text.get());
}
