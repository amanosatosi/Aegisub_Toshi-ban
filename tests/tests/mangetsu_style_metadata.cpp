// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "mangetsu_style_metadata.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_parser.h"
#include "ass_style.h"
#include <gtest/gtest.h>

namespace {
AssStyle& AddStyle(AssFile& file, std::string name = "Default") {
	auto style = new AssStyle; style->name = std::move(name); file.Styles.push_back(*style); return *style;
}
AssDialogue& Add(AssFile& file, std::string text, std::string style = "Default", std::string effect = "mangetsu-style", bool comment = true, std::string actor = "") {
	auto line = new AssDialogue;
	line->Row = static_cast<int>(file.Events.size()); line->Text = std::move(text); line->Style = std::move(style);
	line->Effect = std::move(effect); line->Comment = comment; line->Actor = std::move(actor);
	file.Events.push_back(*line); return *line;
}
}
using mangetsu::StyleMetadataDraft;

TEST(MangetsuStyleMetadata, DefaultsFollowOrdinaryOutlineAndScriptWrap) {
	AssStyle style; style.outline_w = 3.5;
	auto values = StyleMetadataDraft().Values(style, 2);
	EXPECT_EQ(values.at("furisx").value, "50"); EXPECT_FALSE(values.at("furisx").assigned);
	EXPECT_EQ(values.at("furi").value, "1"); EXPECT_EQ(values.at("furiap").value, "1");
	EXPECT_EQ(values.at("q").value, "2"); EXPECT_FALSE(values.at("q").assigned);
	EXPECT_EQ(values.at("1bsx").value, "3.5"); EXPECT_EQ(values.at("10bsx").value, "0");
}
TEST(MangetsuStyleMetadata, StrictTargetsAndInitialSharedBlock) {
	AssFile file; auto& first = AddStyle(file); auto& last = AddStyle(file); auto& sign = AddStyle(file, "Sign");
	EXPECT_EQ(mangetsu::ResolveMetadataStyle(file, "***default"), &last);
	EXPECT_EQ(mangetsu::ResolveMetadataStyle(file, "*Sign"), &sign);
	EXPECT_EQ(mangetsu::ResolveMetadataStyle(file, "sign"), nullptr);
	EXPECT_NE(&first, &last);
	Add(file, "{\\furis60}", "*default");
	Add(file, "{\\1c&HFF0000&}", "Default", "mangetsu-colorcoding", true, "Nene");
	Add(file, "{\\furis70}", "Default");
	Add(file, "note", "Default", "note"); Add(file, "{\\furis20}");
	EXPECT_EQ(mangetsu::InitialMetadata(file).size(), 3u);
	EXPECT_EQ(StyleMetadataDraft(file, last).Values(last, 0).at("furisx").value, "70");
}
TEST(MangetsuStyleMetadata, UnchangedStyleRetainsOriginalTargetSpelling) {
	AssFile file; auto& style = AddStyle(file); auto& line = Add(file, "{\\furis70}", "***default");
	StyleMetadataDraft draft(file, style);
	EXPECT_FALSE(draft.Apply(file, &style, style.name, style, false));
	draft.Set("q", "2"); EXPECT_TRUE(draft.Apply(file, &style, style.name, style, false));
	EXPECT_EQ(line.Style.get(), "***default");
}
TEST(MangetsuStyleMetadata, PresentationHidesOnlyInitialMetadataAndNeverMutatesRows) {
	AssFile file; AddStyle(file);
	auto& style = Add(file, "{\\furis70}");
	auto& actor = Add(file, "{\\1c&HFFFFFF&}", "Default", "mangetsu-colorcoding", true, "Nene");
	auto& ordinary = Add(file, "ordinary", "Default", "note"); auto& late = Add(file, "{\\furis20}");
	auto hidden = mangetsu::HiddenMetadata(file, true, false);
	EXPECT_EQ(hidden.size(), 1u); EXPECT_EQ(hidden.count(&style), 1u); EXPECT_EQ(hidden.count(&actor), 0u);
	EXPECT_EQ(hidden.count(&ordinary), 0u); EXPECT_EQ(hidden.count(&late), 0u);
	EXPECT_TRUE(mangetsu::HiddenMetadata(file, true, true).empty());
	EXPECT_TRUE(mangetsu::HiddenMetadata(file, false, false).empty());
	EXPECT_EQ(file.Events.size(), 4u); EXPECT_EQ(style.Row, 0); EXPECT_EQ(late.Row, 3);
}

TEST(MangetsuStyleMetadata, StableNormalizationKeepsTypesAndInitialBlockContents) {
	AssFile file; AddStyle(file, "Default"); AddStyle(file, "Sign");
	auto& actor1 = Add(file, "{\\1c&H111111&}", "Default", "mangetsu-colorcoding", true, "Nene");
	auto& style1 = Add(file, "{\\furis70}", "Default");
	auto& whitelist = Add(file, "{Default}{Sign}", "mangetsu-colorcode-applied-styles", "mangetsu-colorcoding", true, "mangetsu-colorcode-applied-styles");
	auto& actor2 = Add(file, "{\\1c&H222222&}", "Default", "mangetsu-colorcoding", true, "Mika");
	auto& style2 = Add(file, "{\\q2}", "Sign");
	auto& ordinary = Add(file, "ordinary", "Default", "note");
	auto& late = Add(file, "{\\q0}");
	style1.Row = 1; style2.Row = 4;
	file.Properties.active_row = 0;
	ASSERT_TRUE(mangetsu::NormalizeInitialMetadata(file));
	EXPECT_EQ(file.Properties.active_row, 2);
	auto lines = mangetsu::InitialMetadata(file);
	ASSERT_EQ(lines.size(), 5u);
	EXPECT_EQ(lines[0], &style1); EXPECT_EQ(lines[1], &style2);
	EXPECT_EQ(lines[2], &actor1); EXPECT_EQ(lines[3], &whitelist); EXPECT_EQ(lines[4], &actor2);
	EXPECT_FALSE(mangetsu::NormalizeInitialMetadata(file));
	EXPECT_EQ(ordinary.Row, -1); EXPECT_EQ(late.Row, -1);
}

TEST(MangetsuStyleMetadata, ExternalImportCopiesSelectedStylesAndAllActorDeclarations) {
	AssFile source, destination;
	auto& source_default = AddStyle(source, "Default"); AddStyle(destination, "Default");
	AddStyle(source, "Sign"); AddStyle(destination, "Sign");
	Add(source, "{\\furis70\\grd(10,&HFFFFFF&,&H000000&)}", "Default");
	Add(source, "{\\furis30}", "Sign");
	Add(source, "{\\1c&H111111&}", "Default", "mangetsu-colorcoding", true, "Nene");
	Add(source, "{Default}{Sign}", "mangetsu-colorcode-applied-styles", "mangetsu-colorcoding", true, "mangetsu-colorcode-applied-styles");
	Add(source, "{\\1c&H222222&}", "Default", "mangetsu-colorcoding", true, "Mika");
	Add(source, "late", "Default", "note");
	Add(destination, "{\\q0}", "Default");
	Add(destination, "old actor", "Default", "mangetsu-colorcoding", true, "Old");
	ASSERT_TRUE(mangetsu::ImportMetadata(destination, source, {source_default.name}));
	auto lines = mangetsu::InitialMetadata(destination);
	ASSERT_EQ(lines.size(), 5u);
	EXPECT_NE(lines[0]->Text.get().find("\\furis70"), std::string::npos);
	EXPECT_EQ(lines[1]->Actor.get(), "Old");
	EXPECT_EQ(lines[2]->Actor.get(), "Nene");
	EXPECT_EQ(lines[3]->Actor.get(), "mangetsu-colorcode-applied-styles");
	EXPECT_FALSE(mangetsu::ImportMetadata(destination, source, {source_default.name}));
	EXPECT_EQ(mangetsu::InitialMetadata(destination).size(), 5u);
}
TEST(MangetsuStyleMetadata, RelativeNumbersAndCommonAxisAliasesAreResolvedOnce) {
	AssFile file; auto& style = AddStyle(file);
	Add(file, "{\\furis70\\furisy90\\furis+10\\furifsp-3\\furipos(-2,~+4)\\2bs3\\2bsx+2}");
	auto values = StyleMetadataDraft(file, style).Values(style, 0);
	EXPECT_EQ(values.at("furisx").value, "80"); EXPECT_EQ(values.at("furisy").value, "80");
	EXPECT_EQ(values.at("furifsp").value, "-3"); EXPECT_EQ(values.at("furipos").value, "-2,4");
	EXPECT_EQ(values.at("2bsx").value, "5"); EXPECT_EQ(values.at("2bsy").value, "3");
}
TEST(MangetsuStyleMetadata, NoOpAndEditsPreserveUnknownTextAndGradientOrder) {
	AssFile file; auto& style = AddStyle(file);
	std::string first = "note{\\furis70\\grd(45,&HFF0000&,&H0000FF&)\\t(\\q2)\\img(foo.png)}";
	std::string second = "{\\vc(&HFFFFFF&,&H000000&,&HFFFFFF&,&H000000&)\\q0\\furisbad}";
	Add(file, first); Add(file, second);
	StyleMetadataDraft draft(file, style);
	EXPECT_EQ(draft.Texts(style, 0), (std::vector<std::string>{first, second}));
	draft.Set("furisx", "60"); draft.Set("q", "2");
	auto texts = draft.Texts(style, 0);
	EXPECT_NE(texts[0].find("\\furisy70\\grd(45,"), std::string::npos);
	EXPECT_NE(texts[0].find("\\t(\\q2)\\img(foo.png)"), std::string::npos);
	EXPECT_NE(texts[1].find("\\vc(&HFFFFFF&,&H000000&,&HFFFFFF&,&H000000&)"), std::string::npos);
	EXPECT_NE(texts[1].find("\\furisbad"), std::string::npos);
	EXPECT_EQ(texts[1].find("\\q0"), std::string::npos);
	EXPECT_EQ(draft.Values(style, 0).at("furisy").value, "70");
	EXPECT_EQ(draft.Values(style, 0).at("q").value, "2");
}
TEST(MangetsuStyleMetadata, ResetRemovesAllAssignmentsWithoutChangingScriptOrDialogue) {
	AssFile file; auto& style = AddStyle(file);
	file.SetScriptInfo("WrapStyle", "3");
	Add(file, "{\\q0\\q1}"); Add(file, "{\\q2\\furis70}"); auto& dialogue = Add(file, "{\\q1}visible", "Default", "", false);
	StyleMetadataDraft draft(file, style); draft.Set("q", std::nullopt);
	EXPECT_TRUE(draft.Apply(file, &style, style.name, style, false));
	EXPECT_EQ(file.GetScriptInfoAsInt("WrapStyle"), 3); EXPECT_EQ(dialogue.Text.get(), "{\\q1}visible");
	auto values = StyleMetadataDraft(file, style).Values(style, 3);
	EXPECT_FALSE(values.at("q").assigned); EXPECT_EQ(values.at("q").value, "3");
}
TEST(MangetsuStyleMetadata, CopyAndRenamePreserveSourcesAndNewRowsPrecedeOrdinaryEvents) {
	AssFile file; auto& style = AddStyle(file, "Sign");
	auto& original = Add(file, "{\\furis70\\grd(30,&HFFFFFF&,&H000000&)}", "*Sign");
	auto& actor = Add(file, "{\\1c&HFFFFFF&}", "Default", "mangetsu-colorcoding", true, "Nene");
	Add(file, "ordinary comment", "Default", "note");
	StyleMetadataDraft copy(file, style); auto& other = AddStyle(file, "Copy");
	EXPECT_TRUE(copy.Apply(file, &style, other.name, other, true));
	EXPECT_EQ(original.Style.get(), "*Sign"); EXPECT_EQ(actor.Actor.get(), "Nene");
	EXPECT_EQ(mangetsu::InitialMetadata(file).size(), 3u);
	EXPECT_EQ(StyleMetadataDraft(file, other).Values(other, 0).at("furisx").value, "70");
	StyleMetadataDraft rename(file, style); EXPECT_TRUE(rename.Apply(file, &style, "Renamed", style, false));
	style.name = "Renamed";
	EXPECT_EQ(original.Style.get(), "Renamed");
	EXPECT_TRUE(mangetsu::RemoveStyleMetadata(file, style));
	EXPECT_EQ(mangetsu::InitialMetadata(file).size(), 2u);
	EXPECT_EQ(StyleMetadataDraft(file, other).Values(other, 0).at("furisx").value, "70");
}
TEST(MangetsuStyleMetadata, ChildCopiesAndUnchangedDefaultsDoNotWriteTheDocument) {
	AssFile file; auto& style = AddStyle(file); Add(file, "visible", "Default", "", false);
	StyleMetadataDraft parent(file, style);
	{ auto child = parent; child.Set("q", "2"); }
	EXPECT_FALSE(parent.Dirty()); EXPECT_EQ(file.Events.size(), 1u);
	parent.Set("q", std::nullopt); EXPECT_FALSE(parent.Apply(file, &style, style.name, style, false));
	parent.Set("10bc", "&HFF0000&"); EXPECT_TRUE(parent.Apply(file, &style, style.name, style, false));
	auto values = StyleMetadataDraft(file, style).Values(style, 0);
	EXPECT_EQ(values.at("10bsx").value, "0"); EXPECT_FALSE(values.at("10bsx").assigned);
	EXPECT_TRUE(values.at("10bc").assigned);
}

TEST(MangetsuStyleMetadata, AllTenLayersResolveNativeLimitsAndIndependentChannels) {
	AssFile file; auto& style = AddStyle(file);
	std::string text = "{";
	for (int n = 1; n <= 10; ++n) {
		auto key = std::to_string(n);
		text += '\\' + key + "bs3\\" + key + "bsx+2\\" + key + "bc$white\\" + key + "ba$128\\" + key + "bsc$kuro\\" + key + "bblur500\\" + key + "bbe3.6";
	}
	Add(file, text + '}');
	auto values = StyleMetadataDraft(file, style).Values(style, 0);
	for (int n = 1; n <= 10; ++n) {
		auto key = std::to_string(n);
		EXPECT_EQ(values.at(key + "bsx").value, "5"); EXPECT_EQ(values.at(key + "bsy").value, "3");
		EXPECT_EQ(values.at(key + "bc").value, "&HFFFFFF&"); EXPECT_EQ(values.at(key + "ba").value, "&H80&");
		EXPECT_EQ(values.at(key + "bsc").value, "&H000000&");
		EXPECT_EQ(values.at(key + "bblur").value, "100"); EXPECT_EQ(values.at(key + "bbe").value, "4");
	}
}
TEST(MangetsuStyleMetadata, ExtraBorderPaintAndWaitingDefaultsFollowTheActiveOutline) {
	AssFile file; auto& style = AddStyle(file);
	Add(file, "{\\1bc&H123456&\\1ba&H40&\\2bc&HFFFFFF&\\2bc\\2ba&H80&\\2ba\\10bc&H654321&}");
	auto values = StyleMetadataDraft(file, style).Values(style, 0);
	EXPECT_FALSE(values.at("2bc").assigned); EXPECT_EQ(values.at("2bc").value, "&H123456&");
	EXPECT_FALSE(values.at("2ba").assigned); EXPECT_EQ(values.at("2ba").value, "&H40&");
	EXPECT_FALSE(values.at("2bsc").assigned); EXPECT_EQ(values.at("2bsc").value, "&H123456&");
	EXPECT_FALSE(values.at("10ba").assigned); EXPECT_EQ(values.at("10ba").value, "&H40&");
	EXPECT_FALSE(values.at("10bsc").assigned); EXPECT_EQ(values.at("10bsc").value, "&H654321&");
}

TEST(MangetsuStyleMetadata, MalformedBlocksCannotSwallowNewAssignments) {
	for (std::string text : {"{\\grd(unclosed}", "{{\\furis90}", "{\\furisbad", "{\\t(\\q1)}trailing"}) {
		SCOPED_TRACE(text);
		AssFile file; auto& style = AddStyle(file); Add(file, text);
		StyleMetadataDraft draft(file, style); draft.Set("q", "2");
		auto texts = draft.Texts(style, 0);
		ASSERT_EQ(texts.size(), 2u); EXPECT_EQ(texts.front(), text); EXPECT_EQ(texts.back(), "{\\q2}");
		EXPECT_EQ(draft.Values(style, 0).at("q").value, "2");
	}
}

TEST(MangetsuStyleMetadata, CanonicalNumbersRejectNonfiniteAndTrailingJunk) {
	double value;
	for (auto text : {"nan", "inf", "-inf", "1e999", "12junk", "1,2", ""}) EXPECT_FALSE(mangetsu::ParseStyleNumber(text, value));
	EXPECT_TRUE(mangetsu::ParseStyleNumber("-2.5", value)); EXPECT_EQ(value, -2.5);
	EXPECT_TRUE(mangetsu::ParseStyleNumber("1e2", value)); EXPECT_EQ(value, 100);
}
TEST(MangetsuStyleMetadata, BareOffsetsRestoreAutomaticPlacementInsteadOfExplicitZero) {
	AssFile file; auto& style = AddStyle(file);
	Add(file, "{\\furipos(3,4)\\furipos}");
	StyleMetadataDraft draft(file, style);
	EXPECT_FALSE(draft.Values(style, 0).at("furipos").assigned);
	draft.Set("furipos", "0,0");
	EXPECT_TRUE(draft.Values(style, 0).at("furipos").assigned);
	EXPECT_EQ(draft.Texts(style, 0).back(), "{\\furipos(0,0)}");
}

TEST(MangetsuStyleMetadata, AssSerializationAndReparsePreserveMetadataOrderAndValues) {
	AssFile file; auto& style = AddStyle(file);
	Add(file, "{\\furis70\\grd(45,&HFFFFFF&,&H000000&)}");
	Add(file, "{\\1c&HFFFFFF&}", "Default", "mangetsu-colorcoding", true, "Nene");
	Add(file, "{\\vc(&HFFFFFF&,&H000000&,&HFFFFFF&,&H000000&)}");
	Add(file, "ordinary", "Default", "", false);
	StyleMetadataDraft draft(file, style); draft.Set("q", "3"); draft.Set("10bc", "&H123456&");
	ASSERT_TRUE(draft.Apply(file, &style, style.name, style, false));
	AssFile reopened;
	AssParser parser(&reopened, 1);
	parser.AddLine("[V4+ Styles]"); parser.AddLine(style.GetEntryData()); parser.AddLine("[Events]");
	std::vector<std::string> expected;
	for (auto const& line : file.Events) { auto data = line.GetEntryData(); expected.push_back(data); parser.AddLine(data); }
	std::vector<std::string> actual;
	for (auto const& line : reopened.Events) actual.push_back(line.GetEntryData());
	EXPECT_EQ(actual, expected); ASSERT_EQ(reopened.Styles.size(), 1u);
	auto values = StyleMetadataDraft(reopened, reopened.Styles.front()).Values(reopened.Styles.front(), 0);
	EXPECT_EQ(values.at("q").value, "3"); EXPECT_EQ(values.at("furisx").value, "70");
	EXPECT_EQ(values.at("10bsx").value, "0"); EXPECT_EQ(values.at("10bc").value, "&H123456&");
	EXPECT_EQ(mangetsu::InitialMetadata(reopened).size(), 3u);
}
