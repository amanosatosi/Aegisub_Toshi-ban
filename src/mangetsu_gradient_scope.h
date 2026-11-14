// Scope-aware, component-specific ASS edits for the Mangetsu gradient dialog.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mangetsu {

enum class GradientTargetKind { Primary, Secondary, Outline, Shadow, Fifth };

struct GradientTarget {
	GradientTargetKind kind = GradientTargetKind::Primary;
	int layer = 1; // One-based Mangetsu border layer for Outline.
	bool alpha = false;
};

struct GradientScope {
	size_t start = 0;
	size_t end = 0;
	bool selected = false;
};

struct GradientEdit {
	GradientTarget target;
	bool edit_gradient = true;
	std::string gradient; // Complete tag, or empty to clear the gradient.
	std::string fallback; // Complete style color/alpha tag for post-scope restoration.
	std::string border_x; // Complete numbered border-size tags, if changed.
	std::string border_y;
	std::string border_fallback_x;
	std::string border_fallback_y;
	std::map<std::string, std::string> style_fallbacks;
	std::map<std::string, std::pair<std::string, std::string>> style_border_fallbacks;
};

struct GradientTag {
	std::string name;
	std::string value;
	size_t start = 0;
	size_t end = 0;
	explicit operator bool() const { return end > start; }
};

GradientScope ResolveGradientScope(std::string const& text, size_t selection_start, size_t selection_end);
std::vector<int> FindOutlineLayers(std::string const& text);
GradientTag FindEffectiveGradient(std::string const& text, GradientScope scope, GradientTarget target);
bool IsStyleGradientTag(std::string const& name, GradientTarget target);
std::vector<GradientTag> FindStyleGradients(std::string const& text, GradientTarget target);
// The caret or start of the selection chooses one logical \N line.
int InlineAlignment(std::string const& text, size_t caret);
std::string SetInlineAlignment(std::string const& text, size_t caret, int value, int& selection_start, int& selection_end);
int InheritedInlineAlignment(std::string const& text, size_t caret, int fallback);
std::string GradientSlideWidth(std::string const& text);
std::string SetGradientSlideWidth(std::string text, std::string const& value, GradientScope *scope = nullptr);
GradientTag FindEffectiveBorderSize(std::string const& text, GradientScope scope, int layer, bool x_axis);
double EffectiveBorderSize(std::string const& text, GradientScope scope, int layer, bool x_axis,
	double default_size, std::map<std::string, double> const& named_style_sizes = {});
std::string ApplyGradientEdits(std::string const& original, GradientScope scope,
	std::vector<GradientEdit> const& edits, GradientScope *result_scope = nullptr);

} // namespace mangetsu
