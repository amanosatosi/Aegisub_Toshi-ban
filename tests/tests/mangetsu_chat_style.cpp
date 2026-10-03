#include <main.h>

#include "mangetsu_chat_style.h"

#include <libaegisub/fs.h>
#include <libaegisub/option.h>
#include <libaegisub/option_value.h>

#include <algorithm>
#include <locale>

namespace {
using namespace mangetsu;
std::string const tuple = "(&H030201&,&H04&,&H070605&,&H08&,&H0B0A09&,&H0C&,&H0F0E0D&,&H10&,1.25,&H131211&,&H14&,2.5)";
ChatStyleMask Only(ChatStyleTag tag) { ChatStyleMask mask; mask.set(static_cast<size_t>(tag)); return mask; }
ChatStyleMask All() { ChatStyleMask mask; mask.set(); return mask; }
size_t Count(std::string const& text, std::string const& token) {
	size_t count = 0;
	for (size_t pos = 0; (pos = text.find(token, pos)) != std::string::npos; pos += token.size()) ++count;
	return count;
}
ChatAppearance Sample() {
	auto appearance = DefaultChatAppearance();
	ParseChatBubbleTuple(tuple, appearance.left);
	appearance.right = appearance.left;
	appearance.right.text = {34, 56, 78, 90};
	appearance.panel = {20, 30, 40, 50};
	appearance.title = {1, 100, 200}; appearance.header = {2, 40, 80};
	appearance.automatic_title = appearance.automatic_header = false;
	appearance.read_mark = 1; appearance.read_time = 1245;
	return appearance;
}

TEST(mangetsu_chat_style, parses_left_tuple_in_renderer_field_order) {
	auto loaded = LoadChatStyle("{\\msgleft" + tuple + "}Message", DefaultChatAppearance());
	auto const& side = loaded.appearance.left;
	EXPECT_EQ(agi::Color(1, 2, 3, 4), side.text);
	EXPECT_EQ(agi::Color(5, 6, 7, 8), side.name);
	EXPECT_EQ(agi::Color(9, 10, 11, 12), side.fill);
	EXPECT_EQ(agi::Color(13, 14, 15, 16), side.border);
	EXPECT_DOUBLE_EQ(1.25, side.border_size);
	EXPECT_EQ(agi::Color(17, 18, 19, 20), side.outline);
	EXPECT_DOUBLE_EQ(2.5, side.outline_size);
	EXPECT_EQ(ChatValueOrigin::Line, loaded.origin[static_cast<size_t>(ChatStyleTag::Left)]);
}
TEST(mangetsu_chat_style, parses_right_without_copying_to_left) {
	auto defaults = DefaultChatAppearance();
	auto loaded = LoadChatStyle("{\\msgright" + tuple + "}Message", defaults);
	EXPECT_EQ(agi::Color(1, 2, 3, 4), loaded.appearance.right.text);
	EXPECT_EQ(defaults.left.text, loaded.appearance.left.text);
	EXPECT_EQ(ChatValueOrigin::Default, loaded.origin[static_cast<size_t>(ChatStyleTag::Left)]);
}
TEST(mangetsu_chat_style, serializes_both_sides_with_compact_bgr_alpha_and_sizes) {
	auto appearance = Sample();
	EXPECT_EQ(tuple, SerializeChatBubbleTuple(appearance.left));
	auto output = SerializeChatAppearance(appearance);
	EXPECT_NE(std::string::npos, output.find("\\msgleft" + tuple));
	EXPECT_NE(std::string::npos, output.find("\\msgright(&H4E3822&,&H5A&,"));
	EXPECT_EQ(std::string::npos, output.find('\n'));
	EXPECT_EQ(std::string::npos, output.find(' '));
	auto loaded = LoadChatStyle("{" + output + "}", DefaultChatAppearance());
	EXPECT_EQ(output, SerializeChatAppearance(loaded.appearance));
}
TEST(mangetsu_chat_style, color_and_alpha_round_trip_all_channels) {
	auto input = "{\\4c&H281E14&\\4a&H32&\\msgtitlec&HC86401&\\msgtitlegbc&H502802&\\msgleft" + tuple + "\\msgright" + tuple + "}";
	auto loaded = LoadChatStyle(input, DefaultChatAppearance());
	EXPECT_EQ(agi::Color(20, 30, 40, 50), loaded.appearance.panel);
	EXPECT_EQ(agi::Color(1, 100, 200), loaded.appearance.title);
	EXPECT_EQ(agi::Color(2, 40, 80), loaded.appearance.header);
	EXPECT_EQ(tuple, SerializeChatBubbleTuple(loaded.appearance.left));
	EXPECT_EQ(tuple, SerializeChatBubbleTuple(loaded.appearance.right));
}
TEST(mangetsu_chat_style, numeric_alpha_and_native_named_colors_are_accepted) {
	ChatBubbleStyle side;
	ASSERT_TRUE(ParseChatBubbleTuple("($white,0,$kuro,255,&H123456&,128,FFFFFF,16,0,$shiro,32,1)", side));
	EXPECT_EQ(agi::Color(255, 255, 255, 0), side.text);
	EXPECT_EQ(agi::Color(0, 0, 0, 255), side.name);
	EXPECT_EQ(agi::Color(0x56, 0x34, 0x12, 128), side.fill);
}
TEST(mangetsu_chat_style, invalid_tuples_never_partially_replace_a_side) {
	ChatBubbleStyle side;
	ASSERT_TRUE(ParseChatBubbleTuple(tuple, side));
	for (std::string const& invalid : std::vector<std::string>{"()", "(1,2)", "(" + tuple.substr(1, tuple.size() - 2) + ",3)",
		"(&H030201&,256,&H070605&,0,&H0B0A09&,0,&H0F0E0D&,0,1,&H131211&,0,2)",
		"(&H1000000&,0,&H070605&,0,&H0B0A09&,0,&H0F0E0D&,0,1,&H131211&,0,2)",
		"(&H030201&,0,&H070605&,0,&H0B0A09&,0,&H0F0E0D&,0,nan,&H131211&,0,2)"}) {
		EXPECT_FALSE(ParseChatBubbleTuple(invalid, side)) << invalid;
		EXPECT_EQ(tuple, SerializeChatBubbleTuple(side));
	}
}
TEST(mangetsu_chat_style, tuple_sizes_follow_renderer_clamping) {
	ChatBubbleStyle side;
	ASSERT_TRUE(ParseChatBubbleTuple("(&H00&,0,&H00&,0,&H00&,0,&H00&,0,-2,&H00&,0,10001)", side));
	EXPECT_DOUBLE_EQ(0, side.border_size); EXPECT_DOUBLE_EQ(10000, side.outline_size);
}
TEST(mangetsu_chat_style, tuple_clear_restores_defaults_for_just_that_side) {
	auto loaded = LoadChatStyle("{\\msgleft" + tuple + "\\msgright" + tuple + "\\msgleft()}", DefaultChatAppearance());
	EXPECT_EQ(DefaultChatAppearance().left.text, loaded.appearance.left.text);
	EXPECT_EQ(tuple, SerializeChatBubbleTuple(loaded.appearance.right));
}
TEST(mangetsu_chat_style, read_marks_and_compact_delay_round_trip) {
	for (int mark = 0; mark < 3; ++mark) {
		auto loaded = LoadChatStyle("{\\readmark" + std::to_string(mark) + "\\readtime1245}", DefaultChatAppearance());
		EXPECT_EQ(mark, loaded.appearance.read_mark); EXPECT_EQ(1245, loaded.appearance.read_time);
		auto output = ApplyChatStyle("message", loaded.appearance, All()).text;
		EXPECT_NE(std::string::npos, output.find("\\readmark" + std::to_string(mark) + "\\readtime1245"));
		EXPECT_EQ(std::string::npos, output.find("\\readtime("));
	}
}
TEST(mangetsu_chat_style, invalid_receipt_values_leave_previous_state) {
	auto loaded = LoadChatStyle("{\\readmark1\\readtime1245\\readmark3\\readtime-1\\readtime(20)\\readtime2147483648}", DefaultChatAppearance());
	EXPECT_EQ(1, loaded.appearance.read_mark); EXPECT_EQ(1245, loaded.appearance.read_time);
}
TEST(mangetsu_chat_style, defaults_remain_unauthored_and_opening_is_a_no_op) {
	auto defaults = DefaultChatAppearance({1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}, 3.5);
	std::string original = "{\\k20\\msg(Miku)}Hello\\Nworld";
	auto loaded = LoadChatStyle(original, defaults);
	EXPECT_FALSE(loaded.has_chat_mode); EXPECT_TRUE(loaded.authored.none());
	EXPECT_EQ(defaults.left.text, loaded.appearance.left.text);
	EXPECT_EQ(defaults.panel, loaded.appearance.panel);
	EXPECT_EQ(original, ApplyChatStyle(original, loaded.appearance, {}).text);
}
TEST(mangetsu_chat_style, ordinary_leading_paint_supplies_side_defaults) {
	auto loaded = LoadChatStyle("{\\c&H123456&\\1a&H42&\\2c&H112233&\\3c&H332211&\\bubbs1.75\\bc&HFF0000&\\bs2.25}", DefaultChatAppearance());
	EXPECT_EQ(agi::Color(0x56, 0x34, 0x12, 0x42), loaded.appearance.left.text);
	EXPECT_EQ(loaded.appearance.left.text, loaded.appearance.right.text);
	EXPECT_EQ(agi::Color(0x11, 0x22, 0x33), loaded.appearance.left.fill);
	EXPECT_DOUBLE_EQ(1.75, loaded.appearance.left.border_size);
	EXPECT_DOUBLE_EQ(2.25, loaded.appearance.left.outline_size);
	EXPECT_EQ(agi::Color(0, 0, 255), loaded.appearance.left.outline);
}
TEST(mangetsu_chat_style, panel_alpha_respects_source_order) {
	auto a = LoadChatStyle("{\\4a&H11&\\alpha&H22&}", DefaultChatAppearance());
	auto b = LoadChatStyle("{\\alpha&H22&\\4a&H11&}", DefaultChatAppearance());
	EXPECT_EQ(0x22, a.appearance.panel.a); EXPECT_EQ(0x11, b.appearance.panel.a);
}
TEST(mangetsu_chat_style, automatic_header_follows_panel_until_authored) {
	auto appearance = DefaultChatAppearance();
	EXPECT_EQ(agi::Color(255, 255, 255), appearance.header); EXPECT_EQ(agi::Color(), appearance.title);
	appearance.panel = {255, 255, 255}; RefreshAutomaticChatHeader(appearance);
	EXPECT_EQ(agi::Color(), appearance.header); EXPECT_EQ(agi::Color(255, 255, 255), appearance.title);
	appearance.header = {10, 20, 30}; appearance.automatic_header = false;
	appearance.panel = {}; RefreshAutomaticChatHeader(appearance);
	EXPECT_EQ(agi::Color(10, 20, 30), appearance.header);
}
TEST(mangetsu_chat_style, bare_header_tags_restore_automatic_fallback) {
	auto loaded = LoadChatStyle("{\\msgtitlec&H123456&\\msgtitlegbc&H123456&\\msgtitlec\\msgtitlegbc}", DefaultChatAppearance());
	EXPECT_TRUE(loaded.appearance.automatic_header); EXPECT_TRUE(loaded.appearance.automatic_title);
}
TEST(mangetsu_chat_style, unrelated_content_is_byte_preserved_while_styles_update) {
	std::string untouched = "\\pos(12,34)\\k20\\t(0,400,\\4c&H123456&\\msgleft" + tuple + ")\\msgtitle(Miku)\\msgm(Miku)\\msgtime(1000,2000)\\msgstartcount(1)\\msganim250";
	std::string payload = "{\\msg(Yurf)}Hey\\Nthere{\\i1} |Miku:\\NYeah|{\\4c&H112233&\\4a&HAA&\\bord4}body";
	auto input = "{\\chatmode2\\4c&H000000&" + untouched + "\\msgleft" + tuple + "\\readmark2\\readtime0}" + payload;
	auto output = ApplyChatStyle(input, Sample(), All()).text;
	EXPECT_NE(std::string::npos, output.find(untouched));
	EXPECT_EQ(payload, output.substr(output.size() - payload.size()));
	EXPECT_NE(std::string::npos, output.find("\\chatmode2"));
	EXPECT_EQ(2u, Count(output, "\\msgleft(")); // one inside the untouched transform
	EXPECT_EQ(1u, Count(output, "\\readtime"));
}
TEST(mangetsu_chat_style, nested_tags_are_not_loaded_or_edited) {
	std::string original = "{\\t(0,100,\\readmark0\\msgleft" + tuple + ")\\msg(title\\msgright" + tuple + ")}Message";
	auto loaded = LoadChatStyle(original, DefaultChatAppearance());
	EXPECT_TRUE(loaded.authored.none());
	auto output = ApplyChatStyle(original, Sample(), Only(ChatStyleTag::ReadMark)).text;
	EXPECT_NE(std::string::npos, output.find(original.substr(1, original.find('}') - 1)));
}
TEST(mangetsu_chat_style, existing_style_tags_are_replaced_without_duplication) {
	std::string input = "{\\msgleft" + tuple + "\\msgleft" + tuple + "\\msgright" + tuple + "\\readtime20}hey{\\readtime30}there";
	auto output = ApplyChatStyle(input, Sample(), All()).text;
	EXPECT_EQ(1u, Count(output, "\\msgleft(")); EXPECT_EQ(1u, Count(output, "\\msgright("));
	EXPECT_EQ(1u, Count(output, "\\readtime"));
	EXPECT_NE(std::string::npos, output.find("hey{}there"));
	EXPECT_EQ(output, ApplyChatStyle(output, Sample(), All()).text);
}
TEST(mangetsu_chat_style, editing_one_component_preserves_other_style_tags) {
	std::string input = "{\\4a&H88&\\msgleft" + tuple + "\\msgright" + tuple + "\\readmark0}text";
	auto output = ApplyChatStyle(input, Sample(), Only(ChatStyleTag::PanelColor)).text;
	EXPECT_NE(std::string::npos, output.find("\\4a&H88&\\msgleft" + tuple + "\\msgright" + tuple + "\\readmark0"));
	EXPECT_EQ(1u, Count(output, "\\4c"));
}
TEST(mangetsu_chat_style, applying_never_inserts_chat_mode_or_chat_content) {
	auto output = ApplyChatStyle("ordinary\\Nmessage", Sample(), All()).text;
	EXPECT_EQ(std::string::npos, output.find("\\chatmode"));
	EXPECT_EQ(std::string::npos, output.find("\\msg("));
	EXPECT_EQ(std::string::npos, output.find("\\msgtitle("));
	EXPECT_EQ("ordinary\\Nmessage", output.substr(output.find('}') + 1));
}
TEST(mangetsu_chat_style, selection_mapping_tracks_existing_tag_replacements) {
	std::string input = "{\\readtime20\\msgleft" + tuple + "}Hello\\Nworld";
	auto edit = ApplyChatStyle(input, Sample(), All());
	int start = static_cast<int>(input.find("Hello")), end = static_cast<int>(input.find("world") + 5);
	EXPECT_EQ(edit.text.find("Hello"), static_cast<size_t>(MapChatTextPosition(start, edit)));
	EXPECT_EQ(edit.text.find("world") + 5, static_cast<size_t>(MapChatTextPosition(end, edit)));
}
TEST(mangetsu_chat_style, named_presets_save_reload_load_and_apply_deterministically) {
	ChatStylePresets presets;
	ASSERT_TRUE(presets.Add("Evening phone", Sample()));
	auto saved = presets.Serialize();
	ChatStylePresets restarted;
	ASSERT_TRUE(restarted.Load(saved));
	auto loaded = restarted.Find("Evening phone"); ASSERT_NE(nullptr, loaded);
	EXPECT_EQ(SerializeChatAppearance(Sample()), SerializeChatAppearance(*loaded));
	EXPECT_EQ(saved, restarted.Serialize());
	auto expected = "{" + SerializeChatAppearance(Sample()) + "}message";
	EXPECT_EQ(expected, ApplyChatStyle("message", *loaded, All()).text);
	auto changed = *loaded; changed.read_time = 500;
	ASSERT_TRUE(restarted.Save("Evening phone", changed));
	ASSERT_TRUE(presets.Load(restarted.Serialize()));
	EXPECT_EQ(500, presets.Find("Evening phone")->read_time);
}
TEST(mangetsu_chat_style, named_presets_survive_user_configuration_restart) {
	static char const defaults[] = R"({"Tool":{"Mangetsu Chat":{"Presets":"{\"version\":1,\"presets\":{}}"}}})";
	agi::fs::path path = "data/options/chat-style-test.json";
	{
		ChatStylePresets presets; ASSERT_TRUE(presets.Add("Phone A", Sample()));
		agi::Options config(path, defaults);
		config.Get("Tool/Mangetsu Chat/Presets")->SetString(presets.Serialize());
		config.Flush();
	}
	{
		agi::Options restarted(path, defaults, agi::Options::FLUSH_SKIP);
		restarted.ConfigUser();
		ChatStylePresets presets;
		ASSERT_TRUE(presets.Load(restarted.Get("Tool/Mangetsu Chat/Presets")->GetString()));
		ASSERT_NE(nullptr, presets.Find("Phone A"));
		EXPECT_EQ(SerializeChatAppearance(Sample()), SerializeChatAppearance(*presets.Find("Phone A")));
	}
	agi::fs::Remove(path);
}
TEST(mangetsu_chat_style, preset_names_cannot_silently_overwrite_another_preset) {
	ChatStylePresets presets;
	ASSERT_TRUE(presets.Add("A", Sample())); ASSERT_TRUE(presets.Add("B", DefaultChatAppearance()));
	EXPECT_FALSE(presets.Add("A", DefaultChatAppearance())); EXPECT_FALSE(presets.Add("   ", Sample()));
	EXPECT_FALSE(presets.Rename("B", "A")); EXPECT_FALSE(presets.Save("missing", Sample()));
	ASSERT_TRUE(presets.Rename("A", "C")); EXPECT_EQ(nullptr, presets.Find("A"));
	ASSERT_TRUE(presets.Delete("B")); EXPECT_EQ(1u, presets.Entries().size());
}
TEST(mangetsu_chat_style, malformed_preset_storage_does_not_discard_good_presets) {
	ChatStylePresets presets; ASSERT_TRUE(presets.Add("Keep", Sample()));
	auto before = presets.Serialize();
	for (auto const& invalid : {"", "{", "{\"version\":2,\"presets\":{}}", "{\"version\":1,\"presets\":{\"bad\":\"{\\\\msgleft()}\"}}"}) {
		EXPECT_FALSE(presets.Load(invalid)); EXPECT_EQ(before, presets.Serialize());
	}
}
TEST(mangetsu_chat_style, presets_contain_appearance_only_and_freeze_automatic_header_colors) {
	ChatStylePresets presets;
	auto loaded = LoadChatStyle("{\\chatmode2\\msgtitle(Miku)\\msgm(Yurf)\\msgtime(20)\\msganim250\\readmark1}message", DefaultChatAppearance());
	ASSERT_TRUE(presets.Add("Automatic", loaded.appearance));
	auto stored = presets.Serialize();
	for (auto const& forbidden : {"chatmode", "msgtitle(", "msgm(", "msgtime(", "msganim", "Miku", "Yurf", "message"})
		EXPECT_EQ(std::string::npos, stored.find(forbidden));
	auto preset = presets.Find("Automatic"); ASSERT_NE(nullptr, preset);
	EXPECT_FALSE(preset->automatic_header); EXPECT_FALSE(preset->automatic_title);
}
TEST(mangetsu_chat_style, swap_exchanges_complete_sides_and_nothing_else) {
	auto appearance = Sample(), original = appearance;
	SwapChatSides(appearance);
	EXPECT_EQ(SerializeChatBubbleTuple(original.right), SerializeChatBubbleTuple(appearance.left));
	EXPECT_EQ(SerializeChatBubbleTuple(original.left), SerializeChatBubbleTuple(appearance.right));
	EXPECT_EQ(original.panel, appearance.panel); EXPECT_EQ(original.title, appearance.title);
	EXPECT_EQ(original.header, appearance.header); EXPECT_EQ(original.read_mark, appearance.read_mark);
	EXPECT_EQ(original.read_time, appearance.read_time);
}
} // namespace
