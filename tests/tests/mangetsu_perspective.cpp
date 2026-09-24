// Copyright (c) 2026

#include <main.h>

#include "ass_dialogue.h"
#include "mangetsu_perspective.h"

namespace {
AssDialogue MakeLine(std::string const& text) {
	AssDialogue line;
	line.Text = text;
	return line;
}
}

TEST(MangetsuPerspective, ParsesRendererCornerOrder) {
	auto state = GetMangetsuPerspective(MakeLine(
		"{\\perspective(-10,-20,110,-15,120,80,-5,90)}ONE TWO THREE FOUR"));
	ASSERT_TRUE(state.enabled);
	EXPECT_EQ(Vector2D(-10, -20), state.corners[0]);
	EXPECT_EQ(Vector2D(110, -15), state.corners[1]);
	EXPECT_EQ(Vector2D(120, 80), state.corners[2]);
	EXPECT_EQ(Vector2D(-5, 90), state.corners[3]);
}

TEST(MangetsuPerspective, MalformedTagIsIgnored) {
	EXPECT_FALSE(GetMangetsuPerspective(MakeLine(
		"{\\perspective(0,0,100,0,100,50,0)}Text")).enabled);
	EXPECT_FALSE(GetMangetsuPerspective(MakeLine(
		"{\\perspective(0,0,100,0,100,50,0,nope)}Text")).enabled);
	EXPECT_FALSE(GetMangetsuPerspective(MakeLine(
		"{\\perspective(1,0,0,0,1,0,0,0,2)}Text")).enabled);
}

TEST(MangetsuPerspective, ResetDisablesStaticState) {
	auto state = GetMangetsuPerspective(MakeLine(
		"{\\perspective(0,0,100,0,100,50,0,50)\\r}Text"));
	EXPECT_FALSE(state.enabled);
}

TEST(MangetsuPerspective, NestedTransformIsNotSelectedForStaticEditing) {
	auto state = GetMangetsuPerspective(MakeLine(
		"{\\t(0,1000,\\perspective(0,0,100,0,100,50,0,50))}Text"));
	EXPECT_FALSE(state.enabled);
}

TEST(MangetsuPerspective, CornerEditChangesOnlyItsPair) {
	std::array<Vector2D, 4> replacements {{
		Vector2D(-5.5, -7.25), Vector2D(105.5, -8.25),
		Vector2D(125.5, 63.25), Vector2D(-10.5, 65.25)
	}};
	std::array<std::string, 4> expected {{
		"{\\bord3\\perspective(-5.5,-7.25,100.000000,0.000000,100.000000,50.000000,0.000000,50.000000)\\blur1}Text",
		"{\\bord3\\perspective(0.000000,0.000000,105.5,-8.25,100.000000,50.000000,0.000000,50.000000)\\blur1}Text",
		"{\\bord3\\perspective(0.000000,0.000000,100.000000,0.000000,125.5,63.25,0.000000,50.000000)\\blur1}Text",
		"{\\bord3\\perspective(0.000000,0.000000,100.000000,0.000000,100.000000,50.000000,-10.5,65.25)\\blur1}Text"
	}};
	for (size_t corner = 0; corner < replacements.size(); ++corner) {
		auto line = MakeLine(
			"{\\bord3\\perspective(0.000000,0.000000,100.000000,0.000000,100.000000,50.000000,0.000000,50.000000)\\blur1}Text");
		auto state = GetMangetsuPerspective(line);
		ASSERT_TRUE(SetMangetsuPerspectiveCorner(state, corner, replacements[corner]));
		ASSERT_TRUE(SetMangetsuPerspective(line, state, static_cast<int>(corner)));
		EXPECT_EQ(expected[corner], line.Text.get());
	}
}

TEST(MangetsuPerspective, ReadingAProvisionalStateDoesNotEditTheLine) {
	auto line = MakeLine("{\\an5\\pos(500,400)}ONE TWO THREE FOUR");
	auto original = line.Text.get();
	auto state = GetMangetsuPerspective(line);
	EXPECT_FALSE(state.enabled);
	EXPECT_EQ(original, line.Text.get());
}

TEST(MangetsuPerspective, FirstEditInsertsWithoutNormalizingOtherTags) {
	auto line = MakeLine("{\\an5\\pos(500,400)\\bord2}ONE TWO THREE FOUR");
	MangetsuPerspectiveState state;
	state.corners = {{Vector2D(-80, -20), Vector2D(80, -20),
		Vector2D(90, 25), Vector2D(-80, 20)}};
	ASSERT_TRUE(SetMangetsuPerspective(line, state, 2));
	EXPECT_EQ("{\\an5\\pos(500,400)\\bord2\\perspective(-80,-20,80,-20,90,25,-80,20)}ONE TWO THREE FOUR",
		line.Text.get());
}

TEST(MangetsuPerspective, ExistingDistortRemainsSeparate) {
	auto line = MakeLine("{\\distort(1,0,1,1,0,1)}Text");
	MangetsuPerspectiveState state;
	state.corners = {{Vector2D(0, 0), Vector2D(100, 0),
		Vector2D(100, 50), Vector2D(0, 50)}};
	ASSERT_TRUE(SetMangetsuPerspective(line, state));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\distort(1,0,1,1,0,1)"));
	EXPECT_NE(std::string::npos, line.Text.get().find("\\perspective(0,0,100,0,100,50,0,50)"));
}

TEST(MangetsuPerspective, PositionAndMoveTagsArePreserved) {
	for (auto const& positioning : {std::string("\\pos(500,400)"),
			std::string("\\move(400,350,600,450,100,900)")}) {
		auto line = MakeLine("{" + positioning + "\\bord2}Text");
		MangetsuPerspectiveState state;
		state.corners = {{Vector2D(-50, -20), Vector2D(50, -20),
			Vector2D(50, 20), Vector2D(-50, 20)}};
		ASSERT_TRUE(SetMangetsuPerspective(line, state));
		EXPECT_NE(std::string::npos, line.Text.get().find(positioning));
	}
}

TEST(MangetsuPerspective, ComplexScriptTextIsNeverSplitOrRewritten) {
	std::string burmese = "မြန်မာ စာတန်းထိုး";
	auto line = MakeLine("{\\an5}" + burmese);
	MangetsuPerspectiveState state;
	state.corners = {{Vector2D(-50, -20), Vector2D(50, -20),
		Vector2D(50, 20), Vector2D(-50, 20)}};
	ASSERT_TRUE(SetMangetsuPerspective(line, state));
	EXPECT_NE(std::string::npos, line.Text.get().find(burmese));
}

TEST(MangetsuPerspective, PlaneHandlesRoundTripWithoutTextDimensions) {
	auto line = MakeLine("{\\an5\\pos(500,400)}Hi");
	MangetsuPerspectiveState state;
	state.corners = {{Vector2D(-90, -60), Vector2D(80, -45),
		Vector2D(110, 65), Vector2D(-115, 55)}};
	ASSERT_TRUE(SolveMangetsuPerspectivePlane(state));
	ASSERT_TRUE(SetMangetsuPerspective(line, state));
	auto saved = line.Text.get();
	EXPECT_NE(std::string::npos, saved.find("\\perspective("));
	for (auto const& text : {"Hi", "A much longer line\\Nand a second line", "BIGGER"}) {
		auto changed = MakeLine(saved);
		changed.Text = saved.substr(0, saved.find('}') + 1) + text;
		auto restored = GetMangetsuPerspective(changed);
		ASSERT_TRUE(restored.enabled);
		ASSERT_TRUE(restored.plane);
		for (size_t i = 0; i < 4; ++i) {
			auto mapped = MapMangetsuPerspective(restored, MangetsuPerspectiveReferenceCorner(i));
			EXPECT_NEAR(state.corners[i].X(), mapped.X(), 0.001);
			EXPECT_NEAR(state.corners[i].Y(), mapped.Y(), 0.001);
		}
	}
	auto resized = MakeLine(saved);
	auto changed_header = saved;
	auto position = changed_header.find("\\pos(500,400)");
	ASSERT_NE(std::string::npos, position);
	changed_header.replace(position, std::string("\\pos(500,400)").size(),
		"\\pos(900,700)\\fs80\\fscx150\\fscy70\\frx20\\fry-15\\frz10");
	resized.Text = changed_header;
	auto unchanged_plane = GetMangetsuPerspective(resized);
	ASSERT_TRUE(unchanged_plane.plane);
	for (size_t i = 0; i < 8; ++i)
		EXPECT_NEAR(state.matrix[i], unchanged_plane.matrix[i], 1e-8);
}

TEST(MangetsuPerspective, LegacyCornersStayLegacyUntilEdited) {
	auto line = MakeLine("{\\perspective(0,0,100,0,100,50,0,50)}Text");
	auto state = GetMangetsuPerspective(line);
	ASSERT_TRUE(state.enabled);
	EXPECT_FALSE(state.plane);
	ASSERT_TRUE(SetMangetsuPerspectiveCorner(state, 0, Vector2D(-10, 0)));
	ASSERT_TRUE(SolveMangetsuPerspectivePlane(state));
	ASSERT_TRUE(SetMangetsuPerspective(line, state));
	EXPECT_TRUE(GetMangetsuPerspective(line).plane);
}
