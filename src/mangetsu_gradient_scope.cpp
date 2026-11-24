#include "mangetsu_gradient_scope.h"
#include "ass_tag_edit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <set>

namespace mangetsu {
namespace {

struct Block { size_t open, close; };

std::vector<Block> blocks(std::string const& text) {
	std::vector<Block> result;
	for (size_t open = text.find('{'); open != std::string::npos; ) {
		size_t close = text.find('}', open + 1);
		if (close == std::string::npos) break;
		result.push_back({open, close});
		open = text.find('{', close + 1);
	}
	return result;
}

std::vector<GradientTag> tags(std::string const& text) {
	std::vector<GradientTag> result;
	for (auto block : blocks(text)) {
		for (size_t pos = block.open + 1; pos < block.close; ) {
			if (text[pos] != '\\') { ++pos; continue; }
			size_t name_end = pos + 1;
			while (name_end < block.close && std::isdigit(static_cast<unsigned char>(text[name_end]))) ++name_end;
			while (name_end < block.close && std::isalpha(static_cast<unsigned char>(text[name_end]))) ++name_end;
			if (name_end == pos + 1) { ++pos; continue; }
			// \r may carry a style name without a delimiter; it is a reset tag.
			if (text[pos + 1] == 'r' && text.compare(pos, 4, "\\rnd") != 0)
				name_end = pos + 2;
			size_t value_start = name_end;
			size_t end = value_start;
			if (end < block.close && text[end] == '(') {
				int depth = 0;
				while (end < block.close) {
					if (text[end] == '(') ++depth;
					if (text[end++] == ')' && --depth == 0) break;
				}
			}
			else {
				while (end < block.close && text[end] != '\\') ++end;
			}
			result.push_back({text.substr(pos, name_end - pos),
				text.substr(value_start, end - value_start), pos, end});
			pos = end;
		}
	}
	return result;
}

std::string prefix(int layer) { return "\\" + std::to_string(layer) + "b"; }

std::string reset_style(GradientTag const& tag) {
	std::string name = tag.value;
	auto first = name.find_first_not_of(" \t");
	if (first == std::string::npos) return {};
	auto last = name.find_last_not_of(" \t");
	name = name.substr(first, last - first + 1);
	std::transform(name.begin(), name.end(), name.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return name;
}

bool numbered_border(std::string const& name, int& layer) {
	if (name.size() < 4 || name.front() != '\\') return false;
	size_t i = 1;
	while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
	if (i == 1 || i == name.size() || name[i] != 'b') return false;
	try { layer = std::stoi(name.substr(1, i - 1)); }
	catch (...) { return false; }
	return layer > 0;
}

bool gradient_name(std::string const& name, GradientTarget target) {
	switch (target.kind) {
		case GradientTargetKind::Primary:
			return target.alpha ? name == "\\1gra" :
				(name == "\\1grd" || name == "\\pgrd" || name == "\\1pgrd");
		case GradientTargetKind::Secondary: return name == (target.alpha ? "\\2gra" : "\\2grd");
		case GradientTargetKind::Shadow: return name == (target.alpha ? "\\4gra" : "\\4grd");
		case GradientTargetKind::Fifth: return name == (target.alpha ? "\\5gra" : "\\5grd");
		case GradientTargetKind::Outline:
			return name == prefix(target.layer) + (target.alpha ? "ga" : "grd") ||
				(target.layer == 1 && name == (target.alpha ? "\\3gra" : "\\3grd"));
	}
	return false;
}

bool color_family(std::string const& name, GradientTarget target) {
	if (gradient_name(name, target)) return true;
	if (target.kind == GradientTargetKind::Outline) {
		if (name == prefix(target.layer) + (target.alpha ? "a" : "c") ||
			name == prefix(target.layer) + (target.alpha ? "va" : "vc")) return true;
		return target.layer == 1 &&
			(name == (target.alpha ? "\\3a" : "\\3c") ||
			 name == (target.alpha ? "\\3va" : "\\3vc"));
	}
	int channel = target.kind == GradientTargetKind::Primary ? 1 :
		target.kind == GradientTargetKind::Secondary ? 2 :
		target.kind == GradientTargetKind::Shadow ? 4 : 5;
	std::string p = "\\" + std::to_string(channel);
	if (name == p + (target.alpha ? "a" : "c") ||
		name == p + (target.alpha ? "va" : "vc")) return true;
	return channel == 1 && !target.alpha && (name == "\\c" || name == "\\vc");
}

bool border_size_name(std::string const& name, int layer, bool x_axis) {
	std::string p = prefix(layer);
	if (name == p + "s" || name == p + (x_axis ? "sx" : "sy")) return true;
	return layer == 1 && (name == "\\bord" || name == (x_axis ? "\\xbord" : "\\ybord"));
}

std::string full_tag(std::string const& text, GradientTag const& tag) {
	return text.substr(tag.start, tag.end - tag.start);
}

} // namespace

GradientScope ResolveGradientScope(std::string const& text, size_t start, size_t end) {
	start = std::min(start, text.size());
	end = std::min(end, text.size());
	if (start > end) std::swap(start, end);
	bool selected = start != end;
	for (auto block : blocks(text)) {
		if (start > block.open && start <= block.close) start = block.close + 1;
		if (end > block.open && end <= block.close) end = selected ? block.open : block.close + 1;
	}
	if (end < start) end = start;
	if (!selected) {
		for (auto block : blocks(text))
			if (block.open == start) start = block.close + 1;
		if (start == text.size() && !text.empty() && text.back() != '}') {
			auto prior_block = text.rfind('}');
			start = prior_block == std::string::npos ? 0 : prior_block + 1;
		}
		end = text.find('{', start);
		if (end == std::string::npos) end = text.size();
	}
	return {start, end, selected};
}

std::vector<int> FindOutlineLayers(std::string const& text) {
	std::set<int> layers{1};
	for (auto const& tag : tags(text)) {
		int layer = 0;
		if (numbered_border(tag.name, layer)) layers.insert(layer);
	}
	return {layers.begin(), layers.end()};
}

bool IsStyleGradientTag(std::string const& name, GradientTarget target) {
	if (target.kind == GradientTargetKind::Primary && !target.alpha && name == "\\grd") return true;
	return name != "\\pgrd" && name != "\\1pgrd" && gradient_name(name, target);
}

std::string GradientSlideWidth(std::string const& text) {
	std::string value;
	size_t end = 0;
	for (auto block : blocks(text)) {
		if (block.open != end) break;
		end = block.close + 1;
	}
	for (auto const& tag : tags(text.substr(0, end))) {
		if (tag.name == "\\grds") value = tag.value;
		if (tag.name == "\\grdsoff" && tag.value.empty()) value = "off";
	}
	return value;
}

std::string SetGradientSlideWidth(std::string text, std::string const& value, GradientScope *scope) {
	size_t end = 0;
	for (auto block : blocks(text)) {
		if (block.open != end) break;
		end = block.close + 1;
	}
	auto edit = [&](size_t first, size_t last, std::string const& replacement) {
		text.replace(first, last - first, replacement);
		if (!scope) return;
		auto map = [&](size_t pos) {
			return agi::ass::MoveTextPositionAfterEdit(static_cast<int>(pos), static_cast<int>(first),
				static_cast<int>(last), static_cast<int>(replacement.size()));
		};
		scope->start = map(scope->start);
		scope->end = map(scope->end);
	};
	auto all = tags(text.substr(0, end));
	for (auto it = all.rbegin(); it != all.rend(); ++it) {
		if (it->name == "\\grds" || (it->name == "\\grdsoff" && it->value.empty()))
			edit(it->start, it->end, "");
	}
	if (value.empty()) return text;
	auto tag = "\\grds" + value;
	if (end) edit(1, 1, tag);
	else edit(0, 0, "{" + tag + "}");
	return text;
}

namespace {
Block LogicalLine(std::string const& text, size_t caret) {
	Block line{0, text.size()};
	auto overrides = blocks(text);
	auto block = overrides.begin();
	for (size_t pos = 0; pos + 1 < text.size(); ++pos) {
		if (block != overrides.end() && pos == block->open) {
			pos = block->close;
			++block;
			continue;
		}
		if (text.compare(pos, 2, "\\N") != 0) continue;
		if (caret < pos + 2) { line.close = pos; break; }
		line.open = ++pos + 1;
	}
	return line;
}

int TagAlignment(GradientTag const& tag) {
	if (tag.name != "\\ta") return 0;
	auto start = tag.value.find_first_not_of(" \t");
	if (start == std::string::npos || tag.value.find_last_not_of(" \t") != start) return 0;
	char value = tag.value[start];
	return value >= '1' && value <= '9' ? value - '0' : 0;
}
}

int InlineAlignment(std::string const& text, size_t caret) {
	auto line = LogicalLine(text, caret);
	for (auto const& tag : tags(text))
		if (tag.start >= line.open && tag.start < line.close)
			if (int value = TagAlignment(tag)) return value;
	return 0;
}

int InheritedInlineAlignment(std::string const& text, size_t caret, int fallback) {
	auto line = LogicalLine(text, caret);
	for (auto const& tag : tags(text)) {
		if (tag.start >= line.open) break;
		if (int value = TagAlignment(tag)) fallback = value;
	}
	return fallback;
}

std::string SetInlineAlignment(std::string const& text, size_t caret, int value, int& selection_start, int& selection_end) {
	if (value < 0 || value > 9 || InlineAlignment(text, caret) == value) return text;
	auto line = LogicalLine(text, caret);
	std::vector<GradientTag> existing;
	for (auto const& tag : tags(text))
		if (tag.start >= line.open && tag.start < line.close && TagAlignment(tag)) existing.push_back(tag);
	std::string result = text;
	auto edit = [&](size_t start, size_t end, std::string const& replacement) {
		result.replace(start, end - start, replacement);
		selection_start = agi::ass::MoveTextPositionAfterEdit(selection_start,
			static_cast<int>(start), static_cast<int>(end), static_cast<int>(replacement.size()));
		selection_end = agi::ass::MoveTextPositionAfterEdit(selection_end,
			static_cast<int>(start), static_cast<int>(end), static_cast<int>(replacement.size()));
	};
	if (!existing.empty()) {
		// A later tag can supply the next logical line's inherited alignment.
		// Replace only the first on set. Inherit clears all explicit tags on
		// the chosen logical line, leaving the earlier inherited value active.
		if (value) edit(existing.front().start, existing.front().end, "\\ta" + std::to_string(value));
		else for (auto it = existing.rbegin(); it != existing.rend(); ++it) {
			size_t first = it->start, last = it->end;
			if (first > 0 && result[first - 1] == '{' && last < result.size() && result[last] == '}') {
				--first;
				++last;
			}
			edit(first, last, "");
		}
	} else if (value) {
		auto tag = "\\ta" + std::to_string(value);
		if (line.open < result.size() && result[line.open] == '{' &&
			result.find('}', line.open + 1) != std::string::npos)
			edit(line.open + 1, line.open + 1, tag);
		else edit(line.open, line.open, "{" + tag + "}");
	}
	return result;
}

std::vector<GradientTag> FindStyleGradients(std::string const& text, GradientTarget target) {
	std::vector<GradientTag> matches;
	for (auto& tag : tags(text))
		if (IsStyleGradientTag(tag.name, target) && tag.value.size() > 2 && tag.value.front() == '(' && tag.value.back() == ')')
			matches.push_back(std::move(tag));
	return matches;
}

GradientTag FindEffectiveGradient(std::string const& text, GradientScope scope, GradientTarget target) {
	GradientTag found;
	size_t limit = scope.selected ? scope.start : scope.end;
	for (auto const& tag : tags(text)) {
		if (tag.start >= limit) break;
		if (tag.name == "\\r") found = {};
		else if (target.alpha && tag.name == "\\alpha") found = {};
		else if (color_family(tag.name, target))
			found = gradient_name(tag.name, target) ? tag : GradientTag{};
	}
	if (found || !scope.selected) return found;
	for (auto const& tag : tags(text)) {
		if (tag.start < scope.start) continue;
		if (tag.start >= scope.end) break;
		if (gradient_name(tag.name, target)) return tag;
	}
	return found;
}

GradientTag FindEffectiveBorderSize(std::string const& text, GradientScope scope, int layer, bool x_axis) {
	GradientTag found;
	size_t limit = scope.selected ? scope.start : scope.end;
	for (auto const& tag : tags(text)) {
		if (tag.start >= limit) break;
		if (tag.name == "\\r") found = {};
		else if (border_size_name(tag.name, layer, x_axis)) found = tag;
	}
	return found;
}

double EffectiveBorderSize(std::string const& text, GradientScope scope, int layer, bool x_axis,
	double default_size, std::map<std::string, double> const& named_style_sizes) {
	double size = default_size;
	size_t limit = scope.selected ? scope.start : scope.end;
	for (auto const& tag : tags(text)) {
		if (tag.start >= limit) break;
		if (tag.name == "\\r") {
			auto named = named_style_sizes.find(reset_style(tag));
			size = named == named_style_sizes.end() ? default_size : named->second;
			continue;
		}
		if (!border_size_name(tag.name, layer, x_axis)) continue;
		std::string value = tag.value;
		auto first = value.find_first_not_of(" \t");
		if (first == std::string::npos) { size = default_size; continue; }
		value.erase(0, first);
		bool relative = value.front() == '~' || value.front() == '+' || value.front() == '-';
		if (value.front() == '~') value.erase(0, 1);
		char *end = nullptr;
		double operand = std::strtod(value.c_str(), &end);
		if (end == value.c_str() || !std::isfinite(operand)) continue;
		while (*end == ' ' || *end == '\t') ++end;
		if (*end) continue;
		size = relative ? size + operand : operand;
	}
	return size;
}

std::string ApplyGradientEdits(std::string const& original, GradientScope scope,
	std::vector<GradientEdit> const& edits, GradientScope *result_scope) {
	if (result_scope) *result_scope = scope;
	if (edits.empty() || scope.start >= scope.end || scope.end > original.size()) return original;
	std::string enter, restore;
	std::vector<std::pair<size_t, size_t>> erase;
	std::map<size_t, std::string> reassert;
	auto const parsed = tags(original);
	for (auto const& edit : edits) {
		// The original effective value at the far edge of the scope must survive
		// target tags which were removed from selected inline override blocks.
		if (edit.edit_gradient) {
			std::string prior = edit.fallback;
			for (auto const& tag : parsed) {
				if (tag.start >= scope.end) break;
				if (tag.name == "\\r") {
					auto named = edit.style_fallbacks.find(reset_style(tag));
					prior = named == edit.style_fallbacks.end() ? edit.fallback : named->second;
				}
				else if (edit.target.alpha && tag.name == "\\alpha")
					prior = edit.fallback.substr(0, edit.fallback.find('&')) + tag.value;
				else if (color_family(tag.name, edit.target)) prior = full_tag(original, tag);
			}
			enter += edit.gradient.empty() ? edit.fallback : edit.gradient;
			if (scope.end < original.size()) restore += prior;
			for (auto const& tag : parsed) {
				if (tag.start >= scope.start && tag.end <= scope.end &&
					(edit.gradient.empty() ? gradient_name(tag.name, edit.target) : color_family(tag.name, edit.target)))
					erase.emplace_back(tag.start, tag.end);
				if (edit.target.alpha && tag.name == "\\alpha" &&
					tag.start >= scope.start && tag.end < scope.end)
					reassert[tag.end] += edit.gradient.empty() ? edit.fallback : edit.gradient;
			}
		}

		if (!edit.border_x.empty() || !edit.border_y.empty()) {
			enter += edit.border_x + edit.border_y;
			if (scope.end < original.size()) {
				auto numeric_size = [](std::string const& full) {
					auto marker = full.find_last_of("xy");
					if (marker == std::string::npos) return 0.0;
					return std::strtod(full.c_str() + marker + 1, nullptr);
				};
				std::map<std::string, double> named_x, named_y;
				for (auto const& named : edit.style_border_fallbacks) {
					named_x[named.first] = numeric_size(named.second.first);
					named_y[named.first] = numeric_size(named.second.second);
				}
				GradientScope end_scope{scope.end, scope.end, false};
				double old_x = EffectiveBorderSize(original, end_scope, edit.target.layer,
					true, numeric_size(edit.border_fallback_x), named_x);
				double old_y = EffectiveBorderSize(original, end_scope, edit.target.layer,
					false, numeric_size(edit.border_fallback_y), named_y);
				auto absolute = [&](bool x_axis, double size) {
					std::ostringstream out;
					out << prefix(edit.target.layer) << (x_axis ? "sx" : "sy")
						<< std::setprecision(15) << size;
					return out.str();
				};
				restore += absolute(true, old_x) + absolute(false, old_y);
			}
			for (auto const& tag : parsed)
				if (tag.start >= scope.start && tag.end <= scope.end &&
					(border_size_name(tag.name, edit.target.layer, true) ||
					 border_size_name(tag.name, edit.target.layer, false)))
					erase.emplace_back(tag.start, tag.end);
		}
	}
	if (enter.empty() && erase.empty()) return original;
	for (auto block : blocks(original)) {
		if (block.open < scope.start || block.close >= scope.end) continue;
		bool removed_tag = false, all_removed = true;
		for (size_t i = block.open + 1; i < block.close; ++i) {
			bool covered = false;
			for (auto const& range : erase)
				if (range.first <= i && i < range.second) { covered = true; removed_tag = true; break; }
			if (!covered && !std::isspace(static_cast<unsigned char>(original[i]))) {
				all_removed = false;
				break;
			}
		}
		if (removed_tag && all_removed) erase.emplace_back(block.open, block.close + 1);
	}
	std::sort(erase.begin(), erase.end());
	erase.erase(std::unique(erase.begin(), erase.end()), erase.end());
	std::map<size_t, std::string> insertions;
	insertions[scope.start] += "{" + enter + "}";
	if (!restore.empty()) insertions[scope.end] += "{" + restore + "}";
	for (auto const& tag : parsed)
		if (tag.name == "\\r" && tag.start >= scope.start && tag.end < scope.end)
			insertions[tag.end] += enter;
	for (auto const& entry : reassert)
		insertions[entry.first] += entry.second;
	std::string out;
	out.reserve(original.size() + enter.size() + restore.size() + 4);
	size_t pos = 0, removal = 0;
	while (pos <= original.size()) {
		if (result_scope && pos == scope.end) result_scope->end = out.size();
		auto insertion = insertions.find(pos);
		if (insertion != insertions.end()) out += insertion->second;
		if (result_scope && pos == scope.start) result_scope->start = out.size();
		if (pos == original.size()) break;
		while (removal < erase.size() && erase[removal].second <= pos) ++removal;
		if (removal < erase.size() && erase[removal].first == pos) {
			pos = erase[removal].second;
			continue;
		}
		out += original[pos++];
	}
	return out;
}

} // namespace mangetsu
