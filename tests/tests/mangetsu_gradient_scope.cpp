#include <main.h>

#include "mangetsu_gradient_scope.h"

#include <utility>

namespace {

using mangetsu::GradientEdit;
using mangetsu::GradientTargetKind;

GradientEdit primary(std::string tag = "\\1grd(0,&H000000&,&HFFFFFF&)") {
	GradientEdit edit;
	edit.target.kind = GradientTargetKind::Primary;
	edit.gradient = std::move(tag);
	edit.fallback = "\\1c&HFFFFFF&";
	return edit;
}

GradientEdit outline(int layer) {
	GradientEdit edit;
	edit.target = {GradientTargetKind::Outline, layer, false};
	edit.gradient = "\\" + std::to_string(layer) + "bgrd(0,&H000000&,&HFFFFFF&)";
	edit.fallback = "\\" + std::to_string(layer) + "bc&H000000&";
	return edit;
}

TEST(mangetsu_gradient_scope, switching_to_outline_does_not_create_primary) {
	std::string text = "{\\bord3}Hello";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("Hello"), text.find("Hello"));
	auto result = mangetsu::ApplyGradientEdits(text, scope, {outline(2)});
	EXPECT_NE(std::string::npos, result.find("\\2bgrd("));
	EXPECT_EQ(std::string::npos, result.find("\\1grd("));
}

TEST(mangetsu_gradient_scope, existing_primary_is_preserved_when_editing_outline) {
	std::string text = "{\\1grd(45,&H111111&,&H222222&)\\bord3}Hello";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("Hello"), text.find("Hello"));
	auto result = mangetsu::ApplyGradientEdits(text, scope, {outline(2)});
	EXPECT_NE(std::string::npos, result.find("\\1grd(45,&H111111&,&H222222&)"));
	EXPECT_NE(std::string::npos, result.find("\\2bgrd("));
}

TEST(mangetsu_gradient_scope, inline_primary_colors_do_not_override_selected_gradient) {
	std::string text = "Hello {\\1c&HFF0000&}bea{\\1c&H00FF00&}utiful world";
	auto start = text.find("bea");
	auto end = text.find(" world");
	auto scope = mangetsu::ResolveGradientScope(text, start, end);
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()});
	EXPECT_NE(std::string::npos, result.find("{\\1grd(0,&H000000&,&HFFFFFF&)}bea"));
	EXPECT_EQ(std::string::npos, result.find("bea{\\1c&H00FF00&}"));
	EXPECT_NE(std::string::npos, result.find("utiful{\\1c&H00FF00&} world"));
}

TEST(mangetsu_gradient_scope, loads_gradient_effective_at_selection_start) {
	std::string text = "{\\1grd(30,&H000000&,&HFFFFFF&)}bea{\\1c&H123456&}utiful";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("bea"), text.size());
	auto tag = mangetsu::FindEffectiveGradient(text, scope,
		{GradientTargetKind::Primary, 1, false});
	ASSERT_TRUE(tag);
	EXPECT_EQ("\\1grd", tag.name);
	EXPECT_EQ("(30,&H000000&,&HFFFFFF&)", tag.value);
}

TEST(mangetsu_gradient_scope, unrelated_channels_remain_identical) {
	std::string text = "{\\2c&H123456&\\3grd(30,&H111111&,&H222222&)\\4c&H654321&}Hello";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("Hello"), text.find("Hello"));
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()});
	EXPECT_NE(std::string::npos, result.find("\\2c&H123456&\\3grd(30,&H111111&,&H222222&)\\4c&H654321&"));
}

TEST(mangetsu_gradient_scope, primary_conflict_family_is_removed_without_touching_shadow) {
	std::string text = "start middle{\\c&H111111&\\1vc(&H222222&,&H333333&)\\4grd(0,&H444444&,&H555555&)}part end";
	auto start = text.find("middle");
	auto scope = mangetsu::ResolveGradientScope(text, start, text.find(" end"));
	mangetsu::GradientScope updated;
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()}, &updated);
	auto selected = result.substr(updated.start, updated.end - updated.start);
	EXPECT_NE(std::string::npos, result.find("\\4grd(0,&H444444&,&H555555&)"));
	EXPECT_NE(std::string::npos, result.find("\\1grd(0,&H000000&,&HFFFFFF&)"));
	EXPECT_EQ(std::string::npos, selected.find("\\c"));
	EXPECT_EQ(std::string::npos, selected.find("\\1vc"));
}

TEST(mangetsu_gradient_scope, selected_text_restores_prior_state_at_end) {
	std::string text = "{\\1c&H123456&}Before middle after";
	auto start = text.find("middle");
	auto end = start + 6;
	auto scope = mangetsu::ResolveGradientScope(text, start, end);
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()});
	EXPECT_NE(std::string::npos, result.find("Before {\\1grd(0,&H000000&,&HFFFFFF&)}middle{\\1c&H123456&} after"));
}

TEST(mangetsu_gradient_scope, applied_selection_tracks_inserted_tags) {
	std::string text = "before middle after";
	auto start = text.find("middle");
	auto scope = mangetsu::ResolveGradientScope(text, start, start + 6);
	mangetsu::GradientScope updated;
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()}, &updated);
	EXPECT_TRUE(updated.selected);
	EXPECT_EQ("middle", result.substr(updated.start, updated.end - updated.start));
}

TEST(mangetsu_gradient_scope, new_outline_has_independent_xy_size) {
	std::string text = "Hello";
	auto scope = mangetsu::ResolveGradientScope(text, 0, 0);
	auto edit = outline(3);
	edit.border_x = "\\3bsx4";
	edit.border_y = "\\3bsy5";
	edit.border_fallback_x = "\\3bsx2";
	edit.border_fallback_y = "\\3bsy2";
	auto result = mangetsu::ApplyGradientEdits(text, scope, {edit});
	EXPECT_NE(std::string::npos, result.find("\\3bgrd("));
	EXPECT_NE(std::string::npos, result.find("\\3bsx4\\3bsy5"));
}

TEST(mangetsu_gradient_scope, opening_and_closing_without_edits_is_exact_noop) {
	std::string text = "{\\1grd(45,&H111111&,&H222222&)}Hello {\\4c&HABCDEF&}world";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("Hello"), text.find("Hello"));
	EXPECT_EQ(text, mangetsu::ApplyGradientEdits(text, scope, {}));
}

TEST(mangetsu_gradient_scope, caret_uses_current_text_run_after_inline_override) {
	std::string text = "First {\\1c&H123456&}second {\\2c&H654321&}third";
	auto start = text.find("second");
	auto scope = mangetsu::ResolveGradientScope(text, start, start);
	EXPECT_EQ(start, scope.start);
	EXPECT_EQ(text.find("{\\2c"), scope.end);
	auto result = mangetsu::ApplyGradientEdits(text, scope, {primary()});
	EXPECT_NE(std::string::npos, result.find("First {\\1c&H123456&}{\\1grd(0,&H000000&,&HFFFFFF&)}second"));
	EXPECT_NE(std::string::npos, result.find("{\\1c&H123456&}{\\2c&H654321&}third"));
}

TEST(mangetsu_gradient_scope, caret_at_initial_override_block_targets_following_text) {
	std::string text = "{\\bord3\\1c&HFFFFFF&}Hello";
	auto scope = mangetsu::ResolveGradientScope(text, 0, 0);
	EXPECT_EQ(text.find("Hello"), scope.start);
	EXPECT_EQ(text.size(), scope.end);
}

TEST(mangetsu_gradient_scope, caret_at_end_targets_last_effective_text_run) {
	std::string text = "first{\\1c&H123456&}second";
	auto scope = mangetsu::ResolveGradientScope(text, text.size(), text.size());
	EXPECT_EQ(text.find("second"), scope.start);
	EXPECT_EQ(text.size(), scope.end);
}

TEST(mangetsu_gradient_scope, discovers_numbered_outline_layers_beyond_three) {
	auto layers = mangetsu::FindOutlineLayers("{\\1bs2\\4bsx5\\9bgrd(0,&H000000&,&HFFFFFF&)}x");
	EXPECT_EQ((std::vector<int>{1, 4, 9}), layers);
}

TEST(mangetsu_gradient_scope, named_style_reset_inside_selection_restores_named_color) {
	std::string text = "before mid{\\rAlternate}dle after";
	auto start = text.find("mid");
	auto end = text.find(" after");
	auto edit = primary();
	edit.style_fallbacks["alternate"] = "\\1c&H123456&";
	auto result = mangetsu::ApplyGradientEdits(text,
		mangetsu::ResolveGradientScope(text, start, end), {edit});
	EXPECT_NE(std::string::npos, result.find("{\\rAlternate\\1grd(0,&H000000&,&HFFFFFF&)}dle"));
	EXPECT_NE(std::string::npos, result.find("dle{\\1c&H123456&} after"));
}

TEST(mangetsu_gradient_scope, clearing_gradient_keeps_inline_solid_color) {
	std::string text = "{\\1grd(0,&H000000&,&HFFFFFF&)}a{\\1c&H123456&}b";
	auto scope = mangetsu::ResolveGradientScope(text, text.find('a'), text.size());
	auto edit = primary("");
	auto result = mangetsu::ApplyGradientEdits(text, scope, {edit});
	EXPECT_NE(std::string::npos, result.find("{\\1c&H123456&}b"));
}

TEST(mangetsu_gradient_scope, outline_size_uses_layer_specific_xy_and_relative_values) {
	std::string text = "{\\bord3\\2bs4\\2bsx+1\\2bsy~-2}Hello";
	auto scope = mangetsu::ResolveGradientScope(text, text.find("Hello"), text.find("Hello"));
	EXPECT_DOUBLE_EQ(3, mangetsu::EffectiveBorderSize(text, scope, 1, true, 2));
	EXPECT_DOUBLE_EQ(5, mangetsu::EffectiveBorderSize(text, scope, 2, true, 0));
	EXPECT_DOUBLE_EQ(2, mangetsu::EffectiveBorderSize(text, scope, 2, false, 0));
	EXPECT_DOUBLE_EQ(0, mangetsu::EffectiveBorderSize("Hello", scope, 3, true, 0));
}

TEST(mangetsu_gradient_scope, selected_outline_size_restores_absolute_effective_size) {
	std::string text = "{\\2bs4}before mid{\\2bsx+1}dle after";
	auto start = text.find("mid");
	auto end = text.find(" after");
	GradientEdit edit;
	edit.target = {GradientTargetKind::Outline, 2, false};
	edit.edit_gradient = false;
	edit.border_x = "\\2bsx10";
	edit.border_y = "\\2bsy10";
	edit.border_fallback_x = "\\2bsx0";
	edit.border_fallback_y = "\\2bsy0";
	auto result = mangetsu::ApplyGradientEdits(text,
		mangetsu::ResolveGradientScope(text, start, end), {edit});
	EXPECT_NE(std::string::npos, result.find("dle{\\2bsx5\\2bsy4} after"));
}

} // namespace
