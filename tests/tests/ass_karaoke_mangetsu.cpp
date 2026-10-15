// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include <main.h>

#include <libaegisub/character_count.h>

#include "ass_dialogue.h"
#include "ass_karaoke.h"

namespace {
AssDialogue make_line(std::string const& text, int duration = 660) {
	AssDialogue line;
	line.Start = 0;
	line.End = duration;
	line.Text = text;
	return line;
}

size_t character_pos(std::string const& text, size_t characters) {
	return agi::IndexOfCharacter(text, characters);
}

void split_three_kana(AssKaraoke& karaoke, bool toshiki) {
	auto split = [&](size_t syllable) {
		auto const& text = (karaoke.begin() + syllable)->text;
		size_t pos = character_pos(text, 1);
		if (toshiki)
			karaoke.AddSplitKTiming(syllable, pos);
		else
			karaoke.AddSplit(syllable, pos);
	};
	split(0);
	split(1);
	karaoke.SetTimingBoundaries(0, 660, {300, 560}, false);
}
}

TEST(AssKaraokeMangetsu, UntimedFuriganaExposesOnlyRubyText) {
	auto line = make_line(u8"<病|やまい>");
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(1u, karaoke.size());
	EXPECT_EQ(std::string(u8"やまい"), karaoke.begin()->text);
	EXPECT_EQ(std::string(u8"<病|やまい>"), karaoke.GetText(false));
}

TEST(AssKaraokeMangetsu, ExistingTimedFuriganaParsesRubySyllables) {
	auto line = make_line(u8"<病|{\\k30}や{\\k26}ま{\\k10}い>");
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(3u, karaoke.size());
	auto syllable = karaoke.begin();
	EXPECT_EQ(std::string(u8"や"), syllable->text);
	EXPECT_EQ(300, syllable->duration);
	EXPECT_EQ("\\k", syllable->tag_type);
	++syllable;
	EXPECT_EQ(std::string(u8"ま"), syllable->text);
	EXPECT_EQ(260, syllable->duration);
	++syllable;
	EXPECT_EQ(std::string(u8"い"), syllable->text);
	EXPECT_EQ(100, syllable->duration);
	EXPECT_EQ(line.Text.get(), karaoke.GetText());
	EXPECT_EQ(std::string(u8"<病|やまい>"), karaoke.GetText(false));
}

TEST(AssKaraokeMangetsu, JoiningRubySyllablesRemovesOnlyTheRubyBoundary) {
	auto line = make_line(u8"<病|{\\k30}や{\\k26}ま{\\k10}い>");
	AssKaraoke karaoke(&line, false, false);

	karaoke.RemoveSplit(1);

	ASSERT_EQ(2u, karaoke.size());
	EXPECT_EQ(std::string(u8"やま"), karaoke.begin()->text);
	EXPECT_EQ(560, karaoke.begin()->duration);
	EXPECT_EQ(std::string(u8"<病|{\\k56}やま{\\k10}い>"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, NormalAndToshikiSplitsSerializeIdenticallyInsideRuby) {
	auto line = make_line(u8"<病|やまい>");
	// These constructor flags match the two controllers' actual load paths.
	AssKaraoke normal(&line, true, true);
	AssKaraoke toshiki(&line, false, false);

	split_three_kana(normal, false);
	split_three_kana(toshiki, true);

	std::string const expected = u8"<病|{\\k30}や{\\k26}ま{\\k10}い>";
	EXPECT_EQ(expected, normal.GetText());
	EXPECT_EQ(expected, toshiki.GetText());
}

TEST(AssKaraokeMangetsu, MixedTextMapsRubyAndOrdinaryTextIndependently) {
	auto line = make_line(u8"君は<病|やまい>じゃない", 600);
	AssKaraoke karaoke(&line, false, false);
	ASSERT_EQ(std::string(u8"君はやまいじゃない"), karaoke.begin()->text);

	karaoke.AddSplitKTiming(0, character_pos(karaoke.begin()->text, 2));
	karaoke.AddSplitKTiming(1, character_pos((karaoke.begin() + 1)->text, 3));
	karaoke.SetTimingBoundaries(0, 600, {100, 400}, false);

	EXPECT_EQ(std::string(u8"{\\k10}君は<病|{\\k30}やまい>{\\k20}じゃない"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, MultipleGroupsKeepIndependentInsertionPoints) {
	auto line = make_line(u8"<病|やまい>と<光|ひかり>", 700);
	AssKaraoke karaoke(&line, false, false);
	ASSERT_EQ(std::string(u8"やまいとひかり"), karaoke.begin()->text);

	karaoke.AddSplitKTiming(0, character_pos(karaoke.begin()->text, 3));
	karaoke.AddSplitKTiming(1, character_pos((karaoke.begin() + 1)->text, 1));
	karaoke.SetTimingBoundaries(0, 700, {300, 400}, false);

	EXPECT_EQ(std::string(u8"<病|{\\k30}やまい>{\\k10}と<光|{\\k30}ひかり>"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, OverrideTagsSurviveAndRubyTagsStayInsideGroup) {
	auto line = make_line(u8"{\\i1}<病|{\\b1}やまい{\\b0}>{\\i0}");
	AssKaraoke karaoke(&line, false, false);
	split_three_kana(karaoke, true);

	EXPECT_EQ(std::string(u8"{\\i1}<病|{\\k30}{\\b1}や{\\k26}ま{\\k10}い{\\b0}>{\\i0}"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, BaseSideIsOpaqueAndRemainsIntact) {
	auto line = make_line(u8"<{\\k99}病|やまい>", 300);
	AssKaraoke karaoke(&line, false, false);

	EXPECT_FALSE(karaoke.HasKaraokeTags());
	karaoke.SetTimingBoundaries(0, 300, {}, false);
	EXPECT_EQ(std::string(u8"<{\\k99}病|{\\k30}やまい>"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, KaraokeTagFamiliesArePreservedByTheSharedModel) {
	auto line = make_line(u8"<病|{\\kf30}や{\\ko26}ま{\\k10}い>");
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(3u, karaoke.size());
	EXPECT_EQ("\\kf", karaoke.begin()->tag_type);
	EXPECT_EQ("\\ko", (karaoke.begin() + 1)->tag_type);
	EXPECT_EQ("\\k", (karaoke.begin() + 2)->tag_type);
	EXPECT_EQ(line.Text.get(), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, UppercaseKUsesExistingAegisubNormalization) {
	auto line = make_line(u8"<病|{\\K30}やまい>", 300);
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(1u, karaoke.size());
	EXPECT_EQ("\\kf", karaoke.begin()->tag_type);
	EXPECT_EQ(std::string(u8"<病|{\\kf30}やまい>"), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, CapitalOKaraokeTagParsesAndStaysInsideRuby) {
	auto line = make_line(u8"<病|{\\kO30}や{\\kO26}ま{\\kO10}い>");
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(3u, karaoke.size());
	EXPECT_EQ("\\kO", karaoke.begin()->tag_type);
	EXPECT_EQ("\\kO", (karaoke.begin() + 1)->tag_type);
	EXPECT_EQ("\\kO", (karaoke.begin() + 2)->tag_type);
	EXPECT_EQ(line.Text.get(), karaoke.GetText());
}

TEST(AssKaraokeMangetsu, OrdinaryKaraokeSerializationIsUnchanged) {
	auto line = make_line("{\\k20}ka{\\kf30}ra{\\ko40}oke", 900);
	AssKaraoke karaoke(&line, false, false);

	EXPECT_EQ(line.Text.get(), karaoke.GetText());
	EXPECT_EQ("karaoke", karaoke.GetText(false));
}

TEST(AssKaraokeMangetsu, MalformedFuriganaFallsBackToOrdinaryText) {
	for (std::string const& text : {
		std::string(u8"<病|やまい"),
		std::string(u8"<|やまい>"),
		std::string(u8"<病|>"),
		std::string(u8"<病|や|まい>"),
		std::string(u8"<病<患|やまい>")
	}) {
		auto line = make_line(text);
		AssKaraoke karaoke(&line, false, false);
		ASSERT_EQ(1u, karaoke.size());
		EXPECT_EQ(text, karaoke.begin()->text);
		EXPECT_EQ(text, karaoke.GetText(false));
	}
}

TEST(AssKaraokeMangetsu, OverrideDelimitersDoNotConfuseFuriganaDetection) {
	auto line = make_line(u8"{\\fnA|B>C}<病|やまい>");
	AssKaraoke karaoke(&line, false, false);

	ASSERT_EQ(1u, karaoke.size());
	EXPECT_EQ(std::string(u8"やまい"), karaoke.begin()->text);
	EXPECT_EQ(line.Text.get(), karaoke.GetText(false));
}
