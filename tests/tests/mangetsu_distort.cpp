// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include <main.h>

#include "ass_dialogue.h"
#include "mangetsu_distort.h"
#include "quad_geometry.h"

namespace {
AssDialogue MakeLine(std::string const& text) {
	AssDialogue line;
	line.Text = text;
	return line;
}

void ExpectPoint(Vector2D actual, double x, double y) {
	EXPECT_NEAR(x, actual.X(), 0.0001);
	EXPECT_NEAR(y, actual.Y(), 0.0001);
}

std::vector<Vector2D> BaseQuad() {
	return {Vector2D(100, 50), Vector2D(300, 50), Vector2D(300, 150), Vector2D(100, 150)};
}
}

TEST(MangetsuDistort, IdentityProducesUndistortedRectangle) {
	auto state = GetMangetsuDistort(MakeLine("{\\distort(1,0,1,1,0,1,0,0)}Text"));
	EXPECT_TRUE(state.enabled);
	EXPECT_TRUE(state.source_eight_values);
	auto quad = DistortQuadToScreen(BaseQuad(), state);

	ASSERT_EQ(4u, quad.size());
	ExpectPoint(quad[0], 100, 50);
	ExpectPoint(quad[1], 300, 50);
	ExpectPoint(quad[2], 300, 150);
	ExpectPoint(quad[3], 100, 150);
}

TEST(MangetsuDistort, IdentityFormattingDoesNotLeakNegativeZero) {
	MangetsuDistortState state;
	state.corners[0] = Vector2D(-0.0f, -0.0000001f);
	EXPECT_EQ("(1,0,1,1,0,1,0,0)", FormatMangetsuDistort(state));
}

TEST(MangetsuDistort, EachCornerEditChangesOnlyItsParameterPair) {
	MangetsuDistortState state;
	auto identity = state.corners;

	ASSERT_TRUE(SetMangetsuDistortCorner(state, 1, Vector2D(1.6f, -0.2f)));
	ExpectPoint(state.corners[1], 1.6, -0.2);
	EXPECT_EQ(identity[0], state.corners[0]);
	EXPECT_EQ(identity[2], state.corners[2]);
	EXPECT_EQ(identity[3], state.corners[3]);

	state = MangetsuDistortState();
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 2, Vector2D(1.4f, 1.3f)));
	ExpectPoint(state.corners[2], 1.4, 1.3);
	EXPECT_EQ(identity[0], state.corners[0]);
	EXPECT_EQ(identity[1], state.corners[1]);
	EXPECT_EQ(identity[3], state.corners[3]);

	state = MangetsuDistortState();
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 3, Vector2D(-0.3f, 1.2f)));
	ExpectPoint(state.corners[3], -0.3, 1.2);
	EXPECT_EQ(identity[0], state.corners[0]);
	EXPECT_EQ(identity[1], state.corners[1]);
	EXPECT_EQ(identity[2], state.corners[2]);

	state = MangetsuDistortState();
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 0, Vector2D(0.2f, 0.1f)));
	ExpectPoint(state.corners[0], 0.2, 0.1);
	EXPECT_EQ(identity[1], state.corners[1]);
	EXPECT_EQ(identity[2], state.corners[2]);
	EXPECT_EQ(identity[3], state.corners[3]);
}

TEST(MangetsuDistort, ParsesLegacyAndExtendedTagsWithoutClamping) {
	auto legacy = GetMangetsuDistort(MakeLine("{\\distort(1.6,-0.2,1.6,1.2,-0.3,1)}Text"));
	EXPECT_TRUE(legacy.enabled);
	EXPECT_FALSE(legacy.source_eight_values);
	ExpectPoint(legacy.corners[0], 0, 0);
	ExpectPoint(legacy.corners[1], 1.6, -0.2);
	ExpectPoint(legacy.corners[2], 1.6, 1.2);
	ExpectPoint(legacy.corners[3], -0.3, 1);

	auto extended = GetMangetsuDistort(MakeLine("{\\distort(1,0,1,1,0,1,0.25,-0.4)}Text"));
	EXPECT_TRUE(extended.enabled);
	EXPECT_TRUE(extended.source_eight_values);
	ExpectPoint(extended.corners[0], 0.25, -0.4);
}

TEST(MangetsuDistort, RelativeCoordinatesResolveAgainstCurrentCornerState) {
	auto state = GetMangetsuDistort(MakeLine(
		"{\\distort(1,0,1,1,0,1,0.2,0.1)\\distort(~+0.5,~-0.2,1,1,0,1,~+0.1,~+0.2)}Text"));

	ExpectPoint(state.corners[0], 0.3, 0.3);
	ExpectPoint(state.corners[1], 1.5, -0.2);
}

TEST(MangetsuDistort, TagRoundTripPreservesNormalizedCoordinates) {
	auto line = MakeLine("{\\an5\\distort(1.6,-0.2,1.6,1.2,0,1,0.2,0.1)\\bord3}Text");
	auto before = GetMangetsuDistort(line);
	ASSERT_TRUE(SetMangetsuDistort(line, before));
	auto after = GetMangetsuDistort(line);

	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR(before.corners[i].X(), after.corners[i].X(), 0.000001);
		EXPECT_NEAR(before.corners[i].Y(), after.corners[i].Y(), 0.000001);
	}
	EXPECT_NE(std::string::npos, line.Text.get().find("\\an5"));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\bord3"));
}

TEST(MangetsuDistort, ExistingTagUpdateChangesOnlySelectedCornerPair) {
	auto line = MakeLine("{\\distort(1.600000,-0.200000,1.234567,1.200000,-0.300000,1.000000,0.250000,-0.400000)}Text");
	auto state = GetMangetsuDistort(line);
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 2, Vector2D(1.5f, 1.25f)));
	ASSERT_TRUE(SetMangetsuDistort(line, state, 2));

	EXPECT_EQ("{\\distort(1.600000,-0.200000,1.5,1.25,-0.300000,1.000000,0.250000,-0.400000)}Text", line.Text.get());
}

TEST(MangetsuDistort, EditingAnyLegacyCornerWritesEightExplicitValues) {
	std::array<std::string, 4> expected {{
		"{\\distort(1,0,1,1,0,1,1.2,-0.1)}Text",
		"{\\distort(1.2,-0.1,1,1,0,1,0,0)}Text",
		"{\\distort(1,0,1.2,-0.1,0,1,0,0)}Text",
		"{\\distort(1,0,1,1,1.2,-0.1,0,0)}Text"
	}};
	for (size_t corner = 0; corner < 4; ++corner) {
		SCOPED_TRACE(corner);
		auto line = MakeLine("{\\distort(1,0,1,1,0,1)}Text");
		auto state = GetMangetsuDistort(line);
		ASSERT_TRUE(SetMangetsuDistortCorner(state, corner, Vector2D(1.2f, -0.1f)));
		ASSERT_TRUE(SetMangetsuDistort(line, state, static_cast<int>(corner)));
		EXPECT_EQ(expected[corner], line.Text.get());
		EXPECT_TRUE(GetMangetsuDistort(line).source_eight_values);
	}
}

TEST(MangetsuDistort, ResetClearsEffectiveDistortionForFirstRenderableUnit) {
	auto reset = GetMangetsuDistort(MakeLine("{\\distort(1.6,-0.2,1.6,1.2,0,1,0.2,0.1)\\r}Text"));
	EXPECT_FALSE(reset.enabled);
	ExpectPoint(reset.corners[0], 0, 0);
	ExpectPoint(reset.corners[1], 1, 0);
	ExpectPoint(reset.corners[2], 1, 1);
	ExpectPoint(reset.corners[3], 0, 1);
}

TEST(MangetsuDistort, StaticEditingPreservesAnimatedDistort) {
	auto line = MakeLine("{\\t(0,1000,\\distort(1,0,1.4,1,-0.4,1,0.2,0.1))}Text");
	auto state = GetMangetsuDistort(line);
	EXPECT_FALSE(state.enabled);
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 2, Vector2D(1.25f, 1.1f)));
	ASSERT_TRUE(SetMangetsuDistort(line, state));

	EXPECT_NE(std::string::npos, line.Text.get().find("\\t(0,1000,\\distort(1,0,1.4,1,-0.4,1,0.2,0.1))"));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\distort(1,0,1.25,1.1,0,1,0,0)"));
}

TEST(MangetsuDistort, DrawingUsesTheSameEffectiveTagState) {
	auto state = GetMangetsuDistort(MakeLine("{\\p1\\distort(1.2,-0.1,1.3,1.1,-0.2,1,0.1,0.2)}m 0 0 l 100 0 100 100 0 100"));
	EXPECT_TRUE(state.enabled);
	ExpectPoint(state.corners[0], 0.1, 0.2);
	ExpectPoint(state.corners[2], 1.3, 1.1);
}

TEST(MangetsuDistort, ScreenProjectionRoundTripUsesQuadGeometry) {
	std::vector<Vector2D> perspective_quad {
		Vector2D(120, 80), Vector2D(330, 60), Vector2D(290, 190), Vector2D(90, 170)
	};
	Vector2D normalized(-0.3f, 1.6f);
	Vector2D screen = UVToXY(perspective_quad, normalized);
	Vector2D round_trip = XYToUV(perspective_quad, screen);

	ExpectPoint(round_trip, -0.3, 1.6);
}

TEST(MangetsuDistort, SixValuePrototypeHasEightParamsButOmittedP0) {
	AssOverrideTag tag("\\distort(1,0,1,1,0,1)");
	ASSERT_TRUE(tag.IsValid());
	ASSERT_EQ(8u, tag.Params.size());
	EXPECT_TRUE(tag.Params[6].omitted);
	EXPECT_TRUE(tag.Params[7].omitted);
	auto line = MakeLine("{\\distort(1,0,1,1,0,1)}Text");
	auto state = GetMangetsuDistort(line);
	ASSERT_TRUE(SetMangetsuDistort(line, state, 3));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1,0,0)}Text", line.Text.get());
}

TEST(MangetsuDistort, UntouchedFormattingAndOtherScopesRemainByteExact) {
	std::string animation = "\\t( 0, 1000,\\distort( 1,0,1.4,1,-0.4,1,0.2,0.1))";
	std::string later = "{\\distort( 2, 0, 2, 2, 0, 2, 0, 0 )\\bord2.000}Later";
	auto line = MakeLine("{\\an5" + animation + "\\distort( 1.00, 0.00, 1.23, 1.00, -.30, 1.00, 0.00, 0.00 ) \\bord3.000}First" + later);
	auto state = GetMangetsuDistort(line);
	ASSERT_TRUE(state.source_eight_values);
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 2, Vector2D(1.5f, 1.25f)));
	ASSERT_TRUE(SetMangetsuDistort(line, state, 2));
	EXPECT_EQ("{\\an5" + animation + "\\distort( 1.00, 0.00,1.5,1.25, -.30, 1.00, 0.00, 0.00 ) \\bord3.000}First" + later, line.Text.get());
}

TEST(MangetsuDistort, NewStaticTagAndIdentityAlwaysHaveExplicitP0) {
	auto line = MakeLine("{\\an5\\t(0,1000,\\distort(1,0,2,2,0,1))}Text");
	ASSERT_TRUE(SetMangetsuDistort(line, MangetsuDistortState(), 1));
	EXPECT_EQ("{\\an5\\t(0,1000,\\distort(1,0,2,2,0,1))\\distort(1,0,1,1,0,1,0,0)}Text", line.Text.get());
}

TEST(MangetsuDistort, EditingAfterResetDoesNotRewriteEarlierTag) {
	auto line = MakeLine("{\\distort(2,0,2,2,0,2,0.3,0.2)\\rAlt}Text");
	auto state = GetMangetsuDistort(line);
	ASSERT_FALSE(state.enabled);
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 1, Vector2D(1.5f, -0.25f)));
	ASSERT_TRUE(SetMangetsuDistort(line, state, 1));
	EXPECT_EQ("{\\distort(2,0,2,2,0,2,0.3,0.2)\\rAlt\\distort(1.5,-0.25,1,1,0,1,0,0)}Text", line.Text.get());
}

TEST(MangetsuDistort, LegacyTagResetsPreviouslyExplicitP0) {
	auto state = GetMangetsuDistort(MakeLine("{\\distort(1,0,1,1,0,1,0.3,0.2)\\distort(1,0,1,1,0,1)}Text"));
	EXPECT_FALSE(state.source_eight_values);
	ExpectPoint(state.corners[0], 0, 0);
}

TEST(MangetsuDistort, SelectedLinesPreserveTheirOwnUneditedLegacyCorners) {
	auto a = MakeLine("{\\distort(1,0,1.3,1.1,-0.2,1)}A");
	auto b = MakeLine("{\\distort(2,0,2.4,2.2,-0.4,2)}B");
	for (auto line : {&a, &b}) {
		auto state = GetMangetsuDistort(*line);
		ASSERT_TRUE(SetMangetsuDistortCorner(state, 1, Vector2D(1.5f, -0.25f)));
		ASSERT_TRUE(SetMangetsuDistort(*line, state, 1));
	}
	EXPECT_EQ("{\\distort(1.5,-0.25,1.3,1.1,-0.2,1,0,0)}A", a.Text.get());
	EXPECT_EQ("{\\distort(1.5,-0.25,2.4,2.2,-0.4,2,0,0)}B", b.Text.get());
}

TEST(MangetsuDistort, MultilineDomainIncludesSpacesNbspAndHardBreaks) {
	std::string source = "{\\distort(1,-0.1,1.2,1.1,-0.1,1.1,0.1,0)}FIRST LINE\\NSECOND MUCH LONGER LINE\\NTHIRD";
	EXPECT_EQ(source, GetMangetsuDistortUnitText(MakeLine(source)));
	source = "{\\distort(1,0,1,1,0,1)}FIRST \xC2\xA0 LINE\\hMORE\r\nSECOND\\nTHIRD";
	// Legacy input is measured as the extended domain which an edit will write.
	EXPECT_EQ(source, GetMangetsuDistortUnitText(MakeLine(source)));
}

TEST(MangetsuDistort, MultilineDomainStopsAtNextEffectiveDistortOrReset) {
	std::string first = "{\\distort(1,0,1,1,0,1,0,0)}FIRST LINE\\NLONGER SECOND LINE";
	EXPECT_EQ(first, GetMangetsuDistortUnitText(MakeLine(first + "{\\distort(2,0,2,2,0,2,0,0)}Later")));
	EXPECT_EQ(first, GetMangetsuDistortUnitText(MakeLine(first + "{\\r}Later")));
	// Identical corners with a different source form still change domains.
	EXPECT_EQ(first, GetMangetsuDistortUnitText(MakeLine(first + "{\\distort(1,0,1,1,0,1)}Later")));
}

TEST(MangetsuDistort, NoOpDistortAndCommentsDoNotSplitDomain) {
	std::string source = "{\\distort(1,0,1,1,0,1,0,0)}FIRST{comment} LINE{\\distort(1,0,1,1,0,1,0,0)}\\NSECOND LINE";
	EXPECT_EQ(source, GetMangetsuDistortUnitText(MakeLine(source)));
}

TEST(MangetsuDistort, DrawingAndRenderRunBoundariesEndDomain) {
	std::string first = "{\\distort(1,0,1,1,0,1,0,0)}FIRST LINE\\NSECOND LINE";
	EXPECT_EQ(first, GetMangetsuDistortUnitText(MakeLine(first + "{\\p1}m 0 0 l 100 100")));
	EXPECT_EQ(first, GetMangetsuDistortUnitText(MakeLine(first + "{\\fs60}Larger")));
	std::string drawing = "{\\p1\\distort(1,0,1,1,0,1,0,0)}m 0 0 l 100 0 100 100";
	EXPECT_EQ(drawing, GetMangetsuDistortUnitText(MakeLine(drawing + "{\\p0}Text")));
}

TEST(MangetsuDistort, RepeatedStyleAndEventGeometryDoNotSplitMultilineDomain) {
	std::string source = "{\\fs30\\distort(1,0,1,1,0,1,0,0)}FIRST LINE{\\fs30}\\NSECOND LINE{\\pos(100,200)\\an5\\q2}\\NTHIRD";
	EXPECT_EQ(source, GetMangetsuDistortUnitText(MakeLine(source)));
}

TEST(MangetsuDistort, ExplicitCornerEditsPreserveAllOtherParameterPairs) {
	std::array<std::string, 4> expected {{
		"( 1.00, 0.00, 1.00, 1.00, 0.00, 1.00,1.5,-0.25)",
		"(1.5,-0.25, 1.00, 1.00, 0.00, 1.00, 0.00, 0.00 )",
		"( 1.00, 0.00,1.5,-0.25, 0.00, 1.00, 0.00, 0.00 )",
		"( 1.00, 0.00, 1.00, 1.00,1.5,-0.25, 0.00, 0.00 )"
	}};
	for (size_t corner = 0; corner < 4; ++corner) {
		SCOPED_TRACE(corner);
		auto line = MakeLine("{\\distort( 1.00, 0.00, 1.00, 1.00, 0.00, 1.00, 0.00, 0.00 )}Text");
		auto state = GetMangetsuDistort(line);
		ASSERT_TRUE(SetMangetsuDistortCorner(state, corner, Vector2D(1.5f, -0.25f)));
		ASSERT_TRUE(SetMangetsuDistort(line, state, static_cast<int>(corner)));
		EXPECT_EQ("{\\distort" + expected[corner] + "}Text", line.Text.get());
	}
}

TEST(MangetsuDistort, EmptyCoordinateSlotsRetainStateAndEightValueSourceForm) {
	auto state = GetMangetsuDistort(MakeLine("{\\distort(1.2,-0.1,1.3,1.1,-0.2,1,0.1,0.2)\\distort(,,,,,,,)}Text"));
	EXPECT_TRUE(state.source_eight_values);
	ExpectPoint(state.corners[0], 0.1, 0.2);
	ExpectPoint(state.corners[1], 1.2, -0.1);
	auto legacy = GetMangetsuDistort(MakeLine("{\\distort(1,0,1,1,0,1,)}Text"));
	EXPECT_TRUE(legacy.enabled);
	EXPECT_FALSE(legacy.source_eight_values);
}

TEST(MangetsuDistort, InvalidCoordinateCountsDoNotBecomeEffectiveTags) {
	for (auto text : {"{\\distort(1,0,1,1,0)}Text", "{\\distort(1,0,1,1,0,1,0.2)}Text", "{\\distort(1,0,1,1,0,1,0,0,9)}Text"})
		EXPECT_FALSE(GetMangetsuDistort(MakeLine(text)).enabled);
}

TEST(MangetsuDistort, MeasurementRetainsMultilineLayoutAndNbsp) {
	auto line = MakeLine("{\\distort(1,0,1,1,0,1,0,0)}FIRST LINE\\NSECOND MUCH LONGER LINE\\hMORE\r\nTHIRD\\nFOURTH");
	EXPECT_EQ("{\\distort(1,0,1,1,0,1,0,0)}FIRST LINE\\NSECOND MUCH LONGER LINE\xC2\xA0MORE\\NTHIRD FOURTH",
		GetMangetsuDistortMeasurementText(line, 0));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1,0,0)}FIRST LINE\\NSECOND MUCH LONGER LINE\xC2\xA0MORE\\NTHIRD\\NFOURTH",
		GetMangetsuDistortMeasurementText(line, 2));
	EXPECT_EQ("{\\q2}ONE\\NTWO", GetMangetsuDistortMeasurementText(MakeLine("{\\q2}ONE\\nTWO"), 0));
	// Measuring a copy never normalizes the authored dialogue.
	EXPECT_NE(std::string::npos, line.Text.get().find("\\hMORE\r\n"));
}

TEST(MangetsuDistort, EditingFirstScopeDoesNotRewriteLaterScopes) {
	std::string later = "{\\distort(2,0,2,2,0,2,0,0)}Later";
	auto line = MakeLine("{\\distort(1,0,1,1,0,1,0,0)}First" + later);
	auto state = GetMangetsuDistort(line);
	ASSERT_TRUE(SetMangetsuDistortCorner(state, 1, Vector2D(1.5f, -0.25f)));
	ASSERT_TRUE(SetMangetsuDistort(line, state));

	EXPECT_NE(std::string::npos, line.Text.get().find("\\distort(1.5,-0.25,1,1,0,1,0,0)"));
	EXPECT_NE(std::string::npos, line.Text.get().find(later));
}

TEST(MangetsuDistort, UnitKeepsNormalSpacesAcrossTheWholeRun) {
	auto line = MakeLine("{\\distort(1,0,1,1,0,1)}ONE TWO THREE FOUR");
	EXPECT_EQ(line.Text.get(), GetMangetsuDistortUnitText(line));
}

TEST(MangetsuDistort, UnitKeepsMultipleSpacesAndNbsp) {
	std::string text = "{\\distort(1,0,1,1,0,1)}ONE   TWO\xC2\xA0THREE";
	EXPECT_EQ(text, GetMangetsuDistortUnitText(MakeLine(text)));
}

TEST(MangetsuDistort, UnitKeepsLeadingWhitespaceAdvance) {
	std::string text = "{\\distort(1,0,1,1,0,1)}  \xC2\xA0ONE TWO";
	EXPECT_EQ(text, GetMangetsuDistortUnitText(MakeLine(text)));
}

TEST(MangetsuDistort, UnitStopsAtEffectiveStyleChange) {
	auto unit = GetMangetsuDistortUnitText(MakeLine("{\\distort(1,0,1,1,0,1)}ONE TWO{\\b1} THREE"));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1)}ONE TWO", unit);
}

TEST(MangetsuDistort, UnitStopsAtDistortChange) {
	auto unit = GetMangetsuDistortUnitText(MakeLine(
		"{\\distort(1,0,1,1,0,1)}ONE TWO{\\distort(1.2,0,1,1,0,1)} THREE"));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1)}ONE TWO", unit);
}

TEST(MangetsuDistort, UnitKeepsHardLineBreakForEightValueAuthoring) {
	auto unit = GetMangetsuDistortUnitText(MakeLine("{\\distort(1,0,1,1,0,1)}ONE TWO\\NTHREE FOUR"));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1)}ONE TWO\\NTHREE FOUR", unit);
}

TEST(MangetsuDistort, UnitRetainsSourceWhitespaceWithoutRewritingText) {
	auto unit = GetMangetsuDistortUnitText(MakeLine("{\\distort(1,0,1,1,0,1)}ONE TWO   "));
	EXPECT_EQ("{\\distort(1,0,1,1,0,1)}ONE TWO   ", unit);
}
