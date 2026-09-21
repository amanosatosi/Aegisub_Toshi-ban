// Copyright (c) 2026

#include <main.h>

#include "ass_tag_edit.h"
#include "better_view.h"

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

TEST(ass_tag_edit, fade_duration_uses_active_line_and_clamps_outside) {
	EXPECT_EQ(420, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1420, 1000, 3000));
	EXPECT_EQ(1580, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 1420, 1000, 3000));
	EXPECT_EQ(0, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1000, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 1000, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 3000, 1000, 3000));
	EXPECT_EQ(0, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 3000, 1000, 3000));
	EXPECT_EQ(0, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 900, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 900, 1000, 3000));
	EXPECT_EQ(2000, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 3500, 1000, 3000));
	EXPECT_EQ(0, agi::ass::FadeDurationFromVideoTime(FadeSide::Out, 3500, 1000, 3000));
	EXPECT_EQ(-1, agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1000, 3000, 1000));
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

TEST(ass_tag_edit, colored_fade_batch_reuses_one_active_line_duration) {
	int const milliseconds = agi::ass::FadeDurationFromVideoTime(FadeSide::In, 1420, 1000, 3000);
	auto first = agi::ass::SetColoredFade("First", FadeSide::In, milliseconds, "&H000000&");
	auto second = agi::ass::SetColoredFade("{\\fad(25,75)}Second", FadeSide::In, milliseconds, "&H000000&");
	EXPECT_EQ("{\\fad(420,0,&H000000&,)}First", first.text);
	EXPECT_EQ("{\\fad(420,75,&H000000&,)}Second", second.text);
}

TEST(ass_tag_edit, colored_fade_updates_first_valid_effective_tag_only) {
	auto result = agi::ass::SetColoredFade(
		"{\\fad(100,200)\\fad(300,400)}Text", FadeSide::In, 50, "&H010203&");
	EXPECT_EQ("{\\fad(50,200,&H010203&,)\\fad(300,400)}Text", result.text);
}

TEST(ass_tag_edit, colored_fade_precedes_an_effective_long_form_fade) {
	auto result = agi::ass::SetColoredFade(
		"{\\bord3\\fade(255,0,255,0,100,900,1000)\\fad(300,400)}Text",
		FadeSide::Out, 75, "&HABCDEF&");
	EXPECT_EQ(
		"{\\bord3\\fad(0,75,,&HABCDEF&)\\fade(255,0,255,0,100,900,1000)\\fad(300,400)}Text",
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
