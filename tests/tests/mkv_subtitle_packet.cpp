// Copyright (c) 2026, Aegisub Project
#include "mkv_subtitle_fixture.h"

#include <gtest/gtest.h>
#include <ass/ass.h>

#include <memory>

TEST(MatroskaTextPacket, AssAndSsaWithoutPaddingAreUnchanged) {
	const std::string packet = "17,2,Default,Actor,10,20,30,Effect,hello, world";
	auto imported = mkv_test::ImportPacket(packet);
	ASSERT_TRUE(imported);
	EXPECT_EQ(imported->first, 17);
	EXPECT_EQ(imported->second,
		"Dialogue: 2,0:00:01.00,0:00:04.00,Default,Actor,10,20,30,Effect,hello, world");
	EXPECT_EQ(matroska::TrimTextPacketEnd(packet.data(), packet.data() + packet.size()),
		packet.data() + packet.size());
}

TEST(MatroskaTextPacket, AssAndSsaTrimOneOrManyNulsBeforeImport) {
	for (int version : {0, 1}) for (size_t padding : {1u, 4u}) {
		SCOPED_TRACE(::testing::Message() << "version=" << version << " padding=" << padding);
		std::string packet = "17,2,Default,Actor,10,20,30,Effect,hello, world";
		packet.append(padding, '\0');
		auto imported = mkv_test::ImportPacket(packet);
		ASSERT_TRUE(imported);
		AssFile file;
		AssParser parser(&file, version);
		parser.AddLine("[Events]");
		parser.AddLine(imported->second);
		ASSERT_EQ(file.Events.size(), 1u);
		auto const& dialogue = file.Events.front();
		EXPECT_EQ(dialogue.Text.get(), "hello, world");
		EXPECT_EQ(dialogue.Layer, 2);
		EXPECT_EQ(dialogue.Start, agi::Time(1000));
		EXPECT_EQ(dialogue.End, agi::Time(4000));
		EXPECT_EQ(dialogue.GetEntryData().find('\0'), std::string::npos);
	}
}

TEST(MatroskaTextPacket, Utf8SrtTrimsPaddingAndPreservesLineBreakConversion) {
	std::string packet = "first\r\nsecond\rthird\nfourth";
	packet.append(3, '\0');
	auto imported = mkv_test::ImportPacket(packet, true);
	ASSERT_TRUE(imported);
	AssDialogue dialogue(imported->second);
	EXPECT_EQ(dialogue.Text.get(), "first\\Nsecond\\Nthird\\Nfourth");
	EXPECT_EQ(dialogue.GetEntryData().find('\0'), std::string::npos);
}

TEST(MatroskaTextPacket, EmptyAndAllNulPacketsAreSkippedForBothFormats) {
	for (bool srt : {false, true}) for (size_t padding : {0u, 1u, 8u}) {
		std::string packet(padding, '\0');
		EXPECT_FALSE(mkv_test::ImportPacket(packet, srt));
	}
}

TEST(MatroskaTextPacket, InteriorNulIsPreservedForBothFormats) {
	std::string text = "before";
	text.push_back('\0');
	text += "after";
	for (bool srt : {false, true}) {
		std::string packet = (srt ? "" : "0,0,Default,,0,0,0,,") + text;
		packet.append(3, '\0');
		auto imported = mkv_test::ImportPacket(packet, srt);
		ASSERT_TRUE(imported);
		AssDialogue dialogue(imported->second);
		EXPECT_EQ(dialogue.Text.get(), text);
		EXPECT_NE(dialogue.GetEntryData().find('\0'), std::string::npos);
	}
}

TEST(MatroskaTextPacket, DecompressedBufferUsesValidByteCountNotCapacity) {
	std::string packet = "0,0,Default,,0,0,0,,decompressed";
	packet.append(4, '\0');
	std::vector<char> buffer(packet.begin(), packet.end());
	buffer.resize(256, 'x');
	auto end = matroska::TrimTextPacketEnd(buffer.data(), buffer.data() + packet.size());
	auto imported = matroska::ParseTextPacket(buffer.data(), end, false,
		agi::Time(1000), agi::Time(4000), 0);
	ASSERT_TRUE(imported);
	EXPECT_EQ(AssDialogue(imported->second).Text.get(), "decompressed");
}

TEST(MatroskaProvider, SerializedImportReachesNormalLibassWithoutNuls) {
	for (size_t padding : {0u, 1u, 4u}) {
		auto script = mkv_test::ImportedScript(padding);
		ASSERT_EQ(script.find('\0'), std::string::npos);
		EXPECT_EQ(script, mkv_test::ImportedScript(0));
		std::unique_ptr<ASS_Library, decltype(&ass_library_done)> library(ass_library_init(), ass_library_done);
		ASSERT_NE(library, nullptr);
		std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track(
			ass_read_memory(library.get(), script.data(), script.size(), nullptr), ass_free_track);
		ASSERT_NE(track, nullptr);
		ASSERT_EQ(track->n_events, 2);
		EXPECT_STREQ(track->events[0].Text, "Matroska dialogue");
		std::unique_ptr<ASS_Renderer, decltype(&ass_renderer_done)> renderer(
			ass_renderer_init(library.get()), ass_renderer_done);
		ASSERT_NE(renderer, nullptr);
		ass_set_frame_size(renderer.get(), 320, 180);
		ass_set_storage_size(renderer.get(), 320, 180);
		ass_set_fonts(renderer.get(), nullptr, "Sans", 1, nullptr, 1);
		int change = 0;
		EXPECT_NE(ass_render_frame(renderer.get(), track.get(), 2000, &change), nullptr);
	}
}
