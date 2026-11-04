// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "mangetsu_style_metadata.h"
#include "mangetsu_metadata.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace mangetsu {
namespace {
std::string Trim(std::string const& s) {
	auto first = s.find_first_not_of(" \t\r\n");
	return first == s.npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string Hex(unsigned value, int width) {
	std::ostringstream out;
	out << "&H" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value << '&';
	return out.str();
}
bool Color(std::string text, unsigned max, std::string& result) {
	text = Trim(text);
	if (!text.empty() && text[0] == '$') {
		auto name = text.substr(1);
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; });
		if (max != 255 && (name == "white" || name == "siro" || name == "shiro")) { result = Hex(0xFFFFFF, 6); return true; }
		if (max != 255 && (name == "black" || name == "kuro")) { result = Hex(0, 6); return true; }
		if (max == 255 && !name.empty() && name.find_first_not_of("0123456789") == name.npos) {
			try { auto n = std::stoul(name); if (n <= 255) { result = Hex(static_cast<unsigned>(n), 2); return true; } } catch (...) { }
		}
		return false;
	}
	if (text.size() >= 2 && text[0] == '&' && (text[1] == 'H' || text[1] == 'h')) text.erase(0, 2);
	if (!text.empty() && text.back() == '&') text.pop_back();
	if (text.empty() || text.find_first_not_of("0123456789abcdefABCDEF") != text.npos) return false;
	try {
		auto value = std::stoul(text, nullptr, 16);
		if (value > max) return false;
		result = Hex(static_cast<unsigned>(value), max == 255 ? 2 : 6);
		return true;
	} catch (...) { return false; }
}
StyleValues Defaults(AssStyle const& style, int wrap) {
	StyleValues v;
	for (auto key : {"furi", "furiap"}) v[key].value = "1";
	for (auto key : {"furisx", "furisy"}) v[key].value = "50";
	for (auto key : {"furifsp", "furistyle", "furiplaceauto", "furichangepos", "vert", "vtype", "vdir", "vsp", "vcolsp"}) v[key].value = "0";
	v["furipos"].value = "0,0";
	v["q"].value = std::to_string(std::max(0, std::min(3, wrap)));
	for (int layer = 1; layer <= 10; ++layer) {
		auto n = std::to_string(layer);
		v[n + "bsx"].value = v[n + "bsy"].value = StyleNumber(layer == 1 ? style.outline_w : 0);
		v[n + "bc"].value = style.outline.GetAssOverrideFormatted();
		v[n + "ba"].value = Hex(style.outline.a, 2);
		v[n + "bsc"].value = style.outline.GetAssOverrideFormatted();
		v[n + "bblur"].value = v[n + "bbe"].value = "0";
	}
	return v;
}
struct Token { size_t start, end; std::string text; };
// Only append inside a complete, unambiguous final override block. Malformed
// hand-written text stays byte-for-byte intact; put the new assignment in a
// separate metadata declaration instead.
bool Appendable(std::string const& text) {
	bool open = false;
	int parentheses = 0;
	if (text.empty() || text.back() != '}') return false;
	for (char c : text) {
		if (c == '{') { if (open) return false; open = true; parentheses = 0; }
		else if (c == '}') { if (!open || parentheses) return false; open = false; }
		else if (open && c == '(') ++parentheses;
		else if (open && c == ')') { if (!parentheses) return false; --parentheses; }
	}
	return !open;
}
std::vector<Token> Tokens(std::string const& text) {
	std::vector<Token> result;
	for (size_t open = text.find('{'); open != text.npos; open = text.find('{', open)) {
		auto close = text.find('}', open + 1);
		if (close == text.npos) break; // Leave malformed blocks untouched.
		if (text.find('{', open + 1) < close) { open = close + 1; continue; }
		for (auto pos = text.find('\\', open + 1); pos < close;) {
			auto end = pos + 1;
			int depth = 0;
			for (; end < close; ++end) {
				if (text[end] == '(') ++depth;
				else if (text[end] == ')' && depth) --depth;
				else if (text[end] == '\\' && !depth) break;
			}
			result.push_back({pos, end, text.substr(pos + 1, end - pos - 1)});
			pos = end;
		}
		open = close + 1;
	}
	return result;
}
bool Number(std::string arg, double base, bool signed_value, double& value) {
	arg = Trim(arg);
	bool relative = !arg.empty() && arg[0] == '~';
	if (relative) {
		arg.erase(0, 1);
		if (arg.empty() || (arg[0] != '+' && arg[0] != '-')) return false;
	}
	relative |= !signed_value && !arg.empty() && (arg[0] == '+' || arg[0] == '-');
	if (!ParseStyleNumber(arg, value)) return false;
	if (relative) value += base;
	if (!std::isfinite(value)) return false;
	if (!signed_value) value = std::max(0.0, value);
	return true;
}
// Return canonical affected fields, not a reconstructed parser tree. Nested
// tags in transforms/gradients are never visited or mistaken for assignments.
std::map<std::string, StyleValue> Assign(Token const& token, StyleValues const& defaults, StyleValues const& current) {
	std::vector<std::string> names;
	for (auto const& entry : defaults) names.push_back(entry.first);
	names.push_back("furis");
	for (int n = 1; n <= 10; ++n) names.push_back(std::to_string(n) + "bs");
	std::sort(names.begin(), names.end(), [](auto const& a, auto const& b) { return a.size() > b.size(); });
	std::string key, arg;
	for (auto const& name : names) if (token.text.compare(0, name.size(), name) == 0) {
		key = name; arg = Trim(token.text.substr(name.size())); break;
	}
	if (key.empty()) return {};
	if (!arg.empty() && arg.front() == '(') {
		if (arg.back() != ')') return {};
		arg = Trim(arg.substr(1, arg.size() - 2));
	}
	std::vector<std::string> fields{key};
	if (key == "furis") fields = {"furisx", "furisy"};
	else if (key.size() >= 3 && key.substr(key.size() - 2) == "bs") fields = {key + 'x', key + 'y'};
	std::map<std::string, StyleValue> result;
	if (key == "furipos") {
		if (arg.empty()) return {{key, {defaults.at(key).value, false}}};
		auto comma = arg.find(',');
		if (comma == arg.npos || arg.find(',', comma + 1) != arg.npos) return {};
		auto prior = current.at(key).value;
		double x, y, px, py;
		ParseStyleNumber(prior.substr(0, prior.find(',')), px);
		ParseStyleNumber(prior.substr(prior.find(',') + 1), py);
		if (!Number(arg.substr(0, comma), px, true, x) || !Number(arg.substr(comma + 1), py, true, y)) return {};
		return {{key, {StyleNumber(x) + ',' + StyleNumber(y), true}}};
	}
	for (auto const& field : fields) {
		if (arg.empty()) {
			bool inherited_paint = field.find("bsc") != field.npos || field.find("bblur") != field.npos || field.find("bbe") != field.npos;
			if (field.size() >= 3 && field != "1bc" && field != "1ba" && (field.back() == 'c' || field.substr(field.size() - 2) == "ba")) inherited_paint = true;
			result[field] = {defaults.at(field).value, !inherited_paint};
			continue;
		}
		bool color = field.back() == 'c';
		bool alpha = field.size() > 2 && field.substr(field.size() - 2) == "ba";
		std::string value;
		if (color || alpha) {
			if (!Color(arg, alpha ? 255 : 0xFFFFFF, value)) return {};
		} else {
			double base = 0, number;
			ParseStyleNumber(current.at(field).value, base);
			bool integer = field == "q" || field == "vert" || field == "vtype" || field == "vdir" || field == "furi" || field == "furiap" || field == "furistyle" || field == "furiplaceauto" || field == "furichangepos";
			bool signed_value = field == "furifsp" || field == "vsp" || field == "vcolsp";
			if (integer) {
				if (!ParseStyleNumber(arg, number) || number != std::floor(number) || number < INT32_MIN || number > INT32_MAX) return {};
				if ((field == "q" || field == "vtype") && (number < 0 || number > 3)) return {};
				if ((field == "vert") && (number < 0 || number > 1)) return {};
				if ((field == "vdir" || field == "furistyle") && (number < 0 || number > 2)) return {};
				if (field == "furi" || field == "furiap" || field == "furiplaceauto") number = number != 0;
				if (field == "furichangepos") number = number == 2 ? 2 : number != 0;
			} else if (!Number(arg, base, signed_value, number)) return {};
			// Native Mangetsu limits at the referenced renderer revision.
			if (field.find("bblur") != field.npos) number = std::min(number, 100.0);
			if (field.find("bbe") != field.npos) number = std::min(std::floor(number + 0.5), 127.0);
			value = key == "furis" && field == "furisy" ? result.at("furisx").value : StyleNumber(number);
		}
		result[field] = {value, true};
	}
	return result;
}
}

std::string StyleNumber(double value) {
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setprecision(15) << value;
	return out.str();
}
bool ParseStyleNumber(std::string const& text, double& value) {
	std::istringstream in(text);
	in.imbue(std::locale::classic());
	in >> value;
	if (!in || !std::isfinite(value)) return false;
	in >> std::ws;
	return in.eof();
}
AssStyle const *ResolveMetadataStyle(AssFile const& file, std::string name) {
	name.erase(0, name.find_first_not_of('*'));
	auto normalized = name;
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; });
	if (normalized == "default") name = "Default";
	for (auto it = file.Styles.rbegin(); it != file.Styles.rend(); ++it) if (it->name == name) return &*it;
	return nullptr;
}
std::vector<AssDialogue*> InitialMetadata(AssFile const& file) {
	std::vector<AssDialogue*> result;
	for (auto const& line : file.Events) {
		if (!IsMetadataComment(line)) break;
		result.push_back(const_cast<AssDialogue*>(&line));
	}
	return result;
}
std::set<AssDialogue const*> HiddenMetadata(AssFile const& file, bool selected, bool show) {
	if (!selected || show) return {};
	auto lines = InitialMetadata(file);
	return {lines.begin(), lines.end()};
}
bool RemoveStyleMetadata(AssFile& file, AssStyle const& style) {
	bool changed = false;
	for (auto line : InitialMetadata(file)) if (line->Effect == "mangetsu-style" && ResolveMetadataStyle(file, line->Style) == &style) {
		delete line;
		changed = true;
	}
	return changed;
}
StyleMetadataDraft::StyleMetadataDraft(AssFile const& file, AssStyle const& style) {
	for (auto line : InitialMetadata(file)) if (line->Effect == "mangetsu-style" && ResolveMetadataStyle(file, line->Style) == &style)
		sources.push_back(line->Text);
}
void StyleMetadataDraft::Set(std::string const& property, std::optional<std::string> value) {
	changes[property] = std::move(value);
}
std::vector<std::string> StyleMetadataDraft::Texts(AssStyle const& style, int wrap) const {
	if (changes.empty()) return sources;
	auto defaults = Defaults(style, wrap), current = defaults;
	std::vector<std::string> result;
	for (auto const& source : sources) {
		std::string rewritten;
		size_t offset = 0;
		for (auto const& token : Tokens(source)) {
			auto assignments = Assign(token, defaults, current);
			bool edit = false;
			for (auto const& entry : assignments) { current[entry.first] = entry.second; edit |= changes.count(entry.first) != 0; }
			if (!edit) continue;
			rewritten += source.substr(offset, token.start - offset);
			for (auto const& entry : assignments) if (!changes.count(entry.first))
				rewritten += '\\' + entry.first + entry.second.value;
			offset = token.end;
		}
		rewritten += source.substr(offset);
		result.push_back(std::move(rewritten));
	}
	std::string append;
	auto pending = changes;
	for (auto const& entry : changes) {
		if (!pending.count(entry.first) || !entry.second) continue;
		auto key = entry.first;
		if (key.back() == 'x' && (key == "furisx" || key.find("bsx") != key.npos)) {
			auto other = key.substr(0, key.size() - 1) + 'y';
			auto it = pending.find(other);
			if (it != pending.end() && it->second == entry.second) { key.pop_back(); pending.erase(it); }
		}
		append += '\\' + key + (key == "furipos" ? '(' + *entry.second + ')' : *entry.second);
	}
	if (!append.empty()) {
		if (!result.empty() && Appendable(result.back()))
			result.back().insert(result.back().size() - 1, append);
		else result.push_back('{' + append + '}');
	}
	return result;
}
StyleValues StyleMetadataDraft::Values(AssStyle const& style, int wrap) const {
	auto defaults = Defaults(style, wrap), current = defaults;
	for (auto const& source : Texts(style, wrap)) for (auto const& token : Tokens(source))
		for (auto const& entry : Assign(token, defaults, current)) current[entry.first] = entry.second;
	// Extra layers inherit the active outline's independent color/alpha
	// channels. An unset waiting color follows the layer's active solid paint.
	for (int n = 1; n <= 10; ++n) {
		auto key = std::to_string(n);
		if (n > 1) for (auto channel : {"bc", "ba"})
			if (!current.at(key + channel).assigned) current[key + channel].value = current.at(std::string("1") + channel).value;
		if (!current.at(key + "bsc").assigned) current[key + "bsc"].value = current.at(key + "bc").value;
	}
	return current;
}
bool StyleMetadataDraft::Apply(AssFile& file, AssStyle const *source, std::string const& name, AssStyle const& style, bool copy) const {
	auto texts = Texts(style, file.GetScriptInfoAsInt("WrapStyle"));
	if (copy && texts.empty()) return false;
	std::vector<AssDialogue*> targets;
	if (!copy && source) for (auto line : InitialMetadata(file))
		if (line->Effect == "mangetsu-style" && ResolveMetadataStyle(file, line->Style) == source) targets.push_back(line);
	bool modified = false;
	for (size_t i = 0; i < texts.size(); ++i) {
		if (i < targets.size()) {
			bool rename = source && source->name != name;
			if (targets[i]->Text != texts[i] || (rename && targets[i]->Style != name)) {
				targets[i]->Text = texts[i];
				if (rename) targets[i]->Style = name;
				modified = true;
			}
		} else {
			auto line = new AssDialogue;
			line->Comment = true; line->Style = name; line->Effect = "mangetsu-style"; line->Text = texts[i];
			auto block = InitialMetadata(file);
			auto where = block.empty() ? file.Events.begin() : std::next(file.Events.iterator_to(*block.back()));
			file.Events.insert(where, *line);
			modified = true;
		}
	}
	return modified;
}
}
