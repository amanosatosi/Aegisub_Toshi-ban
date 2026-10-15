// Copyright (c) 2026

#include <main.h>

#include "ass_tag_edit.h"
#include "better_view.h"
#include <libaegisub/color.h>
#include <limits>
#include <vector>

using agi::ass::FadeSide;

TEST(ass_tag_edit, alignment_inserts_new_block_and_keeps_logical_caret) {
	auto result = agi::ass::SetLineAlignment("Text here", 7, 4, 4);
	EXPECT_EQ("{\\an7}Text here", result.text);
	EXPECT_EQ(10, result.caret);
}

TEST(ass_tag_edit, alignment_replaces_existing_tag_and_keeps_logical_caret) {
	std::string text = "{\\an5}Text here";
	auto result = agi::ass::SetLineAlignment(text, 7, 10, 10);
	EXPECT_EQ("{\\an7}Text here", result.text);
	EXPECT_EQ(10, result.caret);
}

TEST(ass_tag_edit, alignment_preserves_other_initial_tags_and_caret) {
	std::string text = "{\\an5\\bord3}Text here";
	int const caret = static_cast<int>(text.find(" here"));
	auto result = agi::ass::SetLineAlignment(text, 7, caret, caret);
	EXPECT_EQ("{\\an7\\bord3}Text here", result.text);
	EXPECT_EQ(static_cast<int>(result.text.find(" here")), result.caret);
}

TEST(ass_tag_edit, alignment_deletes_selection_before_adding_tag) {
	std::string text = "Hello selected world";
	int const start = static_cast<int>(text.find("selected"));
	int const end = start + 9; // Include the trailing space.
	auto result = agi::ass::SetLineAlignment(text, 7, start, end);
	EXPECT_EQ("{\\an7}Hello world", result.text);
	EXPECT_EQ(static_cast<int>(result.text.find("world")), result.caret);
}

TEST(ass_tag_edit, alignment_deletes_selection_with_existing_override_block) {
	std::string text = "{\\an5\\bord3}Hello selected world";
	int const start = static_cast<int>(text.find("selected"));
	int const end = start + 9;
	auto result = agi::ass::SetLineAlignment(text, 7, start, end);
	EXPECT_EQ("{\\an7\\bord3}Hello world", result.text);
	EXPECT_EQ(static_cast<int>(result.text.find("world")), result.caret);
}

TEST(ass_tag_edit, alignment_caret_round_trips_through_better_view_mapping) {
	std::string text = "Text\\Nhere";
	auto before = agi::BuildBetterViewConversion(text, true);
	int raw_start = 0;
	int raw_end = 0;
	ASSERT_TRUE(before.MapDisplayRangeToRaw(7, 7, raw_start, raw_end));
	auto result = agi::ass::SetLineAlignment(text, 7, raw_start, raw_end);
	auto after = agi::BuildBetterViewConversion(result.text, true);
	EXPECT_EQ(13, after.MapRawToDisplay(result.caret));
}

TEST(ass_tag_edit, fade_duration_is_relative_to_each_lines_start_or_end) {
	EXPECT_EQ(420, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1420, 1000, 3000));
	EXPECT_EQ(1580, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 1420, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1000, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 1000, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 3000, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 3000, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 900, 1000, 3000));
	EXPECT_EQ(2100, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 900, 1000, 3000));
	EXPECT_EQ(2500, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 3500, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 3500, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1000, 3000, 1000));
}

TEST(ass_tag_edit, inapplicable_or_invalid_playhead_edits_leave_tags_and_caret_unchanged) {
	std::string const text = "{\\fad(200,500,&H112233&+a,&H445566&+A)}Text";
	for (auto side : {FadeSide::In, FadeSide::Out}) {
		for (int time : side == FadeSide::In ? std::vector<int>{900, 1000} : std::vector<int>{3000, 3500}) {
			auto result = agi::ass::SetFadeFromVideoTime(text, side, time, 1000, 3000, "&HFFFFFF&");
			EXPECT_EQ(text, result.text);
			EXPECT_EQ(50, agi::ass::MoveTextPositionAfterFadeEdit(50, result));
		}
		EXPECT_EQ(text, agi::ass::SetFadeFromVideoTime(text, side, 2000, 3000, 1000, "").text);
		EXPECT_EQ(text, agi::ass::SetFadeFromVideoTime(text, side, 2000, 2000, 2000, "").text);
	}
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::In,
		std::numeric_limits<int>::max(), -1, std::numeric_limits<int>::max()));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::Out,
		std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max()));
}

TEST(ass_tag_edit, colored_fade_in_preserves_existing_fade_out) {
	auto result = agi::ass::SetColoredFade(
		"{\\fad(200,500)\\bord3}Text", FadeSide::In, 350, "&H0000FF&");
	EXPECT_EQ("{\\fad(350,500,&H0000FF&,)\\bord3}Text", result.text);
}

TEST(ass_tag_edit, colored_fade_out_preserves_existing_start_color) {
	auto result = agi::ass::SetColoredFade(
		"{\\fad(200,500,&H112233&+a,&H445566&)}Text", FadeSide::Out, 600, "&HAABBCC&");
	EXPECT_EQ("{\\fad(200,600,&H112233&+a,&HAABBCC&)}Text", result.text);
	EXPECT_EQ("&H112233&+a", agi::ass::GetFadeColor(result.text, FadeSide::In));
	EXPECT_EQ("&HAABBCC&", agi::ass::GetFadeColor(result.text, FadeSide::Out));
}

TEST(ass_tag_edit, colored_fade_without_existing_tag_uses_zero_for_other_side) {
	auto fade_in = agi::ass::SetColoredFade("Text", FadeSide::In, 350, "&H000000&");
	EXPECT_EQ("{\\fad(350,0,&H000000&,)}Text", fade_in.text);
	auto fade_out = agi::ass::SetColoredFade("{\\bord3}Text", FadeSide::Out, 600, "&HFFFFFF&");
	EXPECT_EQ("{\\fad(0,600,,&HFFFFFF&)\\bord3}Text", fade_out.text);
}

TEST(ass_tag_edit, colored_fade_batch_converts_shared_playhead_to_each_line) {
	auto first = agi::ass::SetFadeFromVideoTime("First", FadeSide::In, 1420, 1000, 3000, "&H000000&");
	auto second = agi::ass::SetFadeFromVideoTime("{\\fad(25,75)}Second", FadeSide::In, 1420, 1100, 4000, "&H000000&");
	EXPECT_EQ("{\\fad(420,0,&H000000&,)}First", first.text);
	EXPECT_EQ("{\\fad(320,75,&H000000&,)}Second", second.text);
}

TEST(ass_tag_edit, colored_fade_updates_first_valid_effective_tag_only) {
	auto result = agi::ass::SetColoredFade(
		"{\\fad(100,200)\\fad(300,400)}Text", FadeSide::In, 50, "&H010203&");
	EXPECT_EQ("{\\fad(50,200,&H010203&,)\\fad(300,400)}Text", result.text);
}

TEST(ass_tag_edit, colored_fade_reuses_existing_fad_before_effective_long_form_fade) {
	auto result = agi::ass::SetColoredFade(
		"{\\bord3\\fade(255,0,255,0,100,900,1000)\\fad(300,400)}Text",
		FadeSide::Out, 75, "&HABCDEF&");
	EXPECT_EQ(
		"{\\bord3\\fad(300,75,,&HABCDEF&)\\fade(255,0,255,0,100,900,1000)}Text",
		result.text);
}

TEST(ass_tag_edit, colored_fade_skips_invalid_tag_and_updates_first_valid_tag) {
	auto result = agi::ass::SetColoredFade(
		"{\\fad(1,2,3)\\fad(300,400,&H111111&,&H222222&)}Text",
		FadeSide::Out, 75, "&HABCDEF&");
	EXPECT_EQ("{\\fad(1,2,3)\\fad(300,75,&H111111&,&HABCDEF&)}Text", result.text);
}

TEST(ass_tag_edit, colored_fade_does_not_treat_transform_fad_as_line_level) {
	auto result = agi::ass::SetColoredFade(
		"{\\t(0,100,\\fad(20,30))\\bord3}Text", FadeSide::In, 40, "&H010101&");
	EXPECT_EQ("{\\fad(40,0,&H010101&,)\\t(0,100,\\fad(20,30))\\bord3}Text", result.text);
}

namespace {
using agi::ass::FadeColorChoice;

// Drive the same picker/apply/commit boundary used by VideoBox, with an
// in-memory selection and undo snapshot instead of a modal wxWidgets dialog.
class FadeOperation : public ::testing::Test {
protected:
	std::vector<std::string> lines{"First", "{\\fad(25,75)}Second"};
	std::vector<int> starts{1000, 1100};
	std::vector<int> ends{3000, 4000};
	int video_time = 1420;
	std::vector<std::vector<std::string>> undo;
	agi::Color picked{0x12, 0x34, 0x56};
	int picker_calls = 0;
	int apply_calls = 0;
	bool accept_picker = true;

	void ThreeLines() {
		video_time = 10000;
		starts = {8000, 9500, 7000};
		ends = {12000, 13000, 10500};
		lines = {"A", "{\\fad(25,75)}B", "{\\fad(35,85)}C"};
	}

	bool Run(FadeSide side, FadeColorChoice choice) {
		auto before = lines;
		return agi::ass::RunFadeOperation(choice,
			[&](std::string& color) {
				++picker_calls;
				color = picked.GetAssOverrideFormatted();
				return accept_picker;
			},
			[&](std::string const& color) {
				++apply_calls;
				for (size_t i = 0; i < lines.size(); ++i)
					lines[i] = agi::ass::SetFadeFromVideoTime(
						lines[i], side, video_time, starts[i], ends[i], color).text;
				return lines != before;
			},
			[&] { undo.push_back(before); });
	}
};

TEST_F(FadeOperation, OrdinaryFadeInCreatesAndUpdatesEachLineWithoutPicker) {
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(420,0)}First", "{\\fad(320,75)}Second"}), lines);
	EXPECT_EQ(0, picker_calls);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, OrdinaryFadeOutCreatesAndUpdatesEachLineWithoutPicker) {
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Normal));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(0,1580)}First", "{\\fad(25,2580)}Second"}), lines);
	EXPECT_EQ(0, picker_calls);
}

TEST_F(FadeOperation, FadeInWhite) {
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::White));
	EXPECT_EQ("{\\fad(420,0,&HFFFFFF&,)}First", lines[0]);
	EXPECT_EQ(0, picker_calls);
}

TEST_F(FadeOperation, FadeInBlack) {
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Black));
	EXPECT_EQ("{\\fad(420,0,&H000000&,)}First", lines[0]);
	EXPECT_EQ(0, picker_calls);
}

TEST_F(FadeOperation, FadeOutWhite) {
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::White));
	EXPECT_EQ("{\\fad(0,1580,,&HFFFFFF&)}First", lines[0]);
	EXPECT_EQ(0, picker_calls);
}

TEST_F(FadeOperation, FadeOutBlack) {
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Black));
	EXPECT_EQ("{\\fad(0,1580,,&H000000&)}First", lines[0]);
	EXPECT_EQ(0, picker_calls);
}

TEST_F(FadeOperation, CustomStartColorUsesBgrAndPicksOnceForSelection) {
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Pick));
	EXPECT_EQ("{\\fad(420,0,&H563412&,)}First", lines[0]);
	EXPECT_EQ("{\\fad(320,75,&H563412&,)}Second", lines[1]);
	EXPECT_EQ(1, picker_calls);
	EXPECT_EQ(1, apply_calls);
	ASSERT_EQ(1u, undo.size());
	lines = undo.back();
	EXPECT_EQ((std::vector<std::string>{"First", "{\\fad(25,75)}Second"}), lines);
}

TEST_F(FadeOperation, CustomEndColorUsesEachLinesDurationAndCommitsOnce) {
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Pick));
	EXPECT_EQ("{\\fad(0,1580,,&H563412&)}First", lines[0]);
	EXPECT_EQ("{\\fad(25,2580,,&H563412&)}Second", lines[1]);
	EXPECT_EQ(1, picker_calls);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, CancellationNeverAppliesOrCommitsEitherSide) {
	accept_picker = false;
	auto before = lines;
	for (auto side : {FadeSide::In, FadeSide::Out}) {
		EXPECT_FALSE(Run(side, FadeColorChoice::Pick));
		EXPECT_EQ(before, lines);
		EXPECT_EQ(0, apply_calls);
		EXPECT_TRUE(undo.empty());
	}
	EXPECT_EQ(2, picker_calls);
}

TEST_F(FadeOperation, UnchangedFadeDoesNotCommit) {
	lines = {"{\\fad(420,75)}Text"};
	EXPECT_FALSE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_TRUE(undo.empty());
}

TEST_F(FadeOperation, ThreeLinesFadeInUseOneVideoTimestampAndDifferentDurations) {
	ThreeLines();
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(2000,0)}A", "{\\fad(500,75)}B", "{\\fad(3000,85)}C"}), lines);
	EXPECT_EQ(0, picker_calls);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, ThreeLinesFadeOutUseOneVideoTimestampAndDifferentDurations) {
	ThreeLines();
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Normal));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(0,2000)}A", "{\\fad(25,3000)}B", "{\\fad(35,500)}C"}), lines);
	EXPECT_EQ(0, picker_calls);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, EachLineKeepsItsOwnOppositeEndColorDurationAndAlphaModifier) {
	ThreeLines();
	lines = {"{\\fad(100,200,&H010101&,&H111111&+a)}A",
		"{\\fad(300,400,&H020202&,&H222222&+A)}B",
		"{\\fad(500,600,&H030303&,&H333333&)}C"};
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(2000,200,,&H111111&+a)}A",
		"{\\fad(500,400,,&H222222&+A)}B", "{\\fad(3000,600,,&H333333&)}C"}), lines);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, SharedCustomColorPreservesEachLinesOppositeStateAndEditedAlphaModifier) {
	ThreeLines();
	lines = {"{\\fad(100,200,&H111111&+a,&H010101&+a)}A",
		"{\\fad(300,400,&H222222&+A,&H020202&)}B",
		"{\\fad(500,600,&H333333&,&H030303&+A)}C"};
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Pick));
	EXPECT_EQ((std::vector<std::string>{"{\\fad(100,2000,&H111111&+a,&H563412&+a)}A",
		"{\\fad(300,3000,&H222222&+A,&H563412&)}B",
		"{\\fad(500,500,&H333333&,&H563412&+A)}C"}), lines);
	EXPECT_EQ(1, picker_calls);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, MixedSelectionSkipsInapplicableFadeInAndInvalidLines) {
	ThreeLines();
	starts = {11000, 9500, 11000};
	ends = {12000, 13000, 10500}; // A starts after playhead; C has invalid timing.
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::White));
	EXPECT_EQ((std::vector<std::string>{"A", "{\\fad(500,75,&HFFFFFF&,)}B", "{\\fad(35,85)}C"}), lines);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, MixedSelectionSkipsInapplicableFadeOutWithoutCopyingActiveTiming) {
	ThreeLines();
	ends = {10000, 13000, 9500}; // Only B ends after the playhead.
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Black));
	EXPECT_EQ((std::vector<std::string>{"A", "{\\fad(25,3000,,&H000000&)}B", "{\\fad(35,85)}C"}), lines);
	EXPECT_EQ(1u, undo.size());
}

TEST_F(FadeOperation, EntirelyInapplicableSelectionCreatesNoUndoEntry) {
	ThreeLines();
	auto before = lines;
	video_time = 7000;
	EXPECT_FALSE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_EQ(before, lines);
	video_time = 13000;
	EXPECT_FALSE(Run(FadeSide::Out, FadeColorChoice::Normal));
	EXPECT_EQ(before, lines);
	EXPECT_TRUE(undo.empty());
}

TEST_F(FadeOperation, PositiveDurationsBeyondLineLengthFollowFadeWorksWithoutClamping) {
	lines = {"First"};
	video_time = 3500;
	ASSERT_TRUE(Run(FadeSide::In, FadeColorChoice::Normal));
	EXPECT_EQ("{\\fad(2500,0)}First", lines[0]);
	video_time = 900;
	ASSERT_TRUE(Run(FadeSide::Out, FadeColorChoice::Normal));
	EXPECT_EQ("{\\fad(2500,2100)}First", lines[0]);
}
}

TEST(ass_tag_edit, normal_fade_preserves_opposite_color_and_alpha_modifier) {
	std::string const text = "{\\fad(200,500,&H112233&+a,&H445566&+A)\\bord3}Text";
	EXPECT_EQ("{\\fad(350,500,,&H445566&+A)\\bord3}Text",
		agi::ass::SetColoredFade(text, FadeSide::In, 350, "").text);
	EXPECT_EQ("{\\fad(200,600,&H112233&+a,)\\bord3}Text",
		agi::ass::SetColoredFade(text, FadeSide::Out, 600, "").text);
}

TEST(ass_tag_edit, colored_to_ordinary_transition_collapses_empty_colors) {
	auto first = agi::ass::SetColoredFade("{\\fad(200,500,&H112233&+a,)}Text", FadeSide::In, 350, "");
	EXPECT_EQ("{\\fad(350,500)}Text", first.text);
	auto colored = agi::ass::SetColoredFade(first.text, FadeSide::Out, 600, "&HFFFFFF&");
	EXPECT_EQ("{\\fad(350,600,,&HFFFFFF&)}Text", colored.text);
	EXPECT_EQ("{\\fad(350,600)}Text",
		agi::ass::SetColoredFade(colored.text, FadeSide::Out, 600, "").text);
}

TEST(ass_tag_edit, color_replacement_preserves_both_alpha_modifiers) {
	std::string const text = "{\\fad(200,500,&H112233&+a,&H445566&+A)}Text";
	EXPECT_EQ("{\\fad(350,500,&HFFFFFF&+a,&H445566&+A)}Text",
		agi::ass::SetColoredFade(text, FadeSide::In, 350, "&HFFFFFF&").text);
	EXPECT_EQ("{\\fad(200,600,&H112233&+a,&H000000&+A)}Text",
		agi::ass::SetColoredFade(text, FadeSide::Out, 600, "&H000000&").text);
}

TEST(ass_tag_edit, normal_fade_updates_existing_tag_and_preserves_selection_mapping) {
	std::string const text = "{\\fad(200,500,&H112233&,)}Text";
	auto result = agi::ass::SetColoredFade(text, FadeSide::In, 350, "");
	EXPECT_EQ("{\\fad(350,500)}Text", result.text);
	EXPECT_EQ(static_cast<int>(result.text.find("Text")),
		agi::ass::MoveTextPositionAfterEdit(static_cast<int>(text.find("Text")),
			result.edit_start, result.edit_end, result.replacement_length));
}

TEST(ass_tag_edit, playhead_edit_maps_active_editor_selection_through_better_view) {
	std::string const text = "{\\fad(200,500,&H112233&,)}Hello\\Nworld";
	auto before = agi::BuildBetterViewConversion(text, true);
	int const start = before.MapRawToDisplay(static_cast<int>(text.find("world")));
	int const end = before.MapRawToDisplay(static_cast<int>(text.size()));
	int raw_start = 0, raw_end = 0;
	ASSERT_TRUE(before.MapDisplayRangeToRaw(start, end, raw_start, raw_end));
	auto result = agi::ass::SetFadeFromVideoTime(text, FadeSide::In, 1420, 1000, 3000, "");
	EXPECT_EQ("{\\fad(420,500)}Hello\\Nworld", result.text);
	auto after = agi::BuildBetterViewConversion(result.text, true);
	EXPECT_EQ(static_cast<int>(result.text.find("world")), agi::ass::MoveTextPositionAfterFadeEdit(raw_start, result));
	EXPECT_EQ(static_cast<int>(result.text.size()), agi::ass::MoveTextPositionAfterFadeEdit(raw_end, result));
	EXPECT_EQ(after.MapRawToDisplay(static_cast<int>(result.text.find("world"))),
		after.MapRawToDisplay(agi::ass::MoveTextPositionAfterFadeEdit(raw_start, result)));
}

TEST(ass_tag_edit, moving_existing_fad_preserves_opposite_color_and_caret_positions) {
	std::string const text = "{\\fade(255,0,255,0,100,900,1000)\\bord3}Middle{\\fad(200,500,&H112233&+a,&H445566&)}Text";
	auto result = agi::ass::SetColoredFade(text, FadeSide::Out, 600, "");
	EXPECT_EQ("{\\fad(200,600,&H112233&+a,)\\fade(255,0,255,0,100,900,1000)\\bord3}MiddleText", result.text);
	for (std::string const& word : {"Middle", "Text", "\\bord3"})
		EXPECT_EQ(static_cast<int>(result.text.find(word)),
			agi::ass::MoveTextPositionAfterFadeEdit(static_cast<int>(text.find(word)), result));
	auto const first_fad = result.text.find("\\fad(");
	ASSERT_NE(std::string::npos, first_fad);
	EXPECT_EQ(std::string::npos, result.text.find("\\fad(", first_fad + 1));
}

TEST(ass_tag_edit, ordinary_fade_without_existing_fad_precedes_long_form_fade) {
	auto result = agi::ass::SetColoredFade("{\\fade(255,0,255,0,100,900,1000)}Text", FadeSide::In, 50, "");
	EXPECT_EQ("{\\fad(50,0)\\fade(255,0,255,0,100,900,1000)}Text", result.text);
}

TEST(ass_tag_edit, invalid_fad_before_long_form_fade_is_repaired_without_duplicate) {
	std::string const text = "{\\fad(1,2,3)}First{\\fade(255,0,255,0,100,900,1000)}Text";
	auto result = agi::ass::SetColoredFade(text, FadeSide::In, 50, "");
	EXPECT_EQ("First{\\fad(50,0)\\fade(255,0,255,0,100,900,1000)}Text", result.text);
	EXPECT_EQ(static_cast<int>(result.text.find("Text")),
		agi::ass::MoveTextPositionAfterFadeEdit(static_cast<int>(text.find("Text")), result));
}
