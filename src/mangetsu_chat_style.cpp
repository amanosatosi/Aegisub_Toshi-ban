#include "mangetsu_chat_style.h"

#include "ass_tag_edit.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace mangetsu {
namespace {
size_t Index(ChatStyleTag tag) { return static_cast<size_t>(tag); }
std::array<std::string, ChatStyleTagCount> const names = {
	"4c", "4a", "msgtitlec", "msgtitlegbc", "msgleft", "msgright", "readmark", "readtime"
};
std::string Trim(std::string value) {
	auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

bool Unsigned(std::string value, unsigned long long limit, unsigned long long& result) {
	value = Trim(value);
	int base = 10;
	if (value.size() >= 2 && value[0] == '&' && (value[1] == 'H' || value[1] == 'h')) {
		base = 16;
		value.erase(0, 2);
		if (!value.empty() && value.back() == '&') value.pop_back();
	}
	if (value.empty()) return false;
	result = 0;
	for (unsigned char c : value) {
		int digit = c >= '0' && c <= '9' ? c - '0' :
			base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 :
			base == 16 && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (digit < 0 || static_cast<unsigned>(digit) > limit ||
			result > (limit - static_cast<unsigned>(digit)) / base) return false;
		result = result * base + digit;
		if (result > limit) return false;
	}
	return true;
}
bool Color(std::string const& value, agi::Color& color) {
	auto hex = Trim(value);
	std::string lower = hex;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (lower == "$white" || lower == "$siro" || lower == "$shiro") hex = "&HFFFFFF&";
	else if (lower == "$black" || lower == "$kuro") hex = "&H000000&";
	else {
		while (!hex.empty() && (hex.front() == '&' || hex.front() == 'H' || hex.front() == 'h')) hex.erase(0, 1);
		if (!hex.empty() && hex.back() == '&') hex.pop_back();
		hex = "&H" + hex + "&";
	}
	unsigned long long bgr;
	if (!Unsigned(hex, 0xFFFFFF, bgr)) return false;
	color.r = bgr & 255;
	color.g = (bgr >> 8) & 255;
	color.b = (bgr >> 16) & 255;
	return true;
}
bool Alpha(std::string const& value, agi::Color& color) {
	auto text = Trim(value);
	if (text.find_first_not_of("0123456789") != std::string::npos) {
		while (!text.empty() && (text.front() == '&' || text.front() == 'H' || text.front() == 'h')) text.erase(0, 1);
		if (!text.empty() && text.back() == '&') text.pop_back();
		text = "&H" + text + "&";
	}
	unsigned long long alpha;
	if (!Unsigned(text, 255, alpha)) return false;
	color.a = static_cast<unsigned char>(alpha);
	return true;
}
bool Size(std::string const& value, double& size) {
	std::istringstream stream(Trim(value));
	stream.imbue(std::locale::classic());
	double number;
	if (!(stream >> number) || !std::isfinite(number)) return false;
	stream >> std::ws;
	if (!stream.eof()) return false;
	size = std::max(0.0, std::min(10000.0, number));
	return true;
}
std::string AlphaString(agi::Color const& color) {
	std::ostringstream stream;
	stream << "&H" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
		<< static_cast<unsigned>(color.a) << '&';
	return stream.str();
}
std::string SizeString(double value) {
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(15) << (std::isfinite(value) ? std::max(0.0, std::min(10000.0, value)) : 0);
	return stream.str();
}

struct Tag { std::string name, value; size_t start, end; bool prefix; };
// Like gradient scope edits, keep source ranges rather than regenerating ASS
// blocks: Aegisub's standard tag prototypes do not know these native-chat tags.
// Balanced parenthesized payloads (including \t and \msg) are opaque.
std::vector<Tag> Tags(std::string const& text) {
	std::vector<Tag> result;
	bool prefix = true;
	size_t cursor = 0;
	while (cursor < text.size()) {
		if (text[cursor] != '{') { prefix = false; ++cursor; continue; }
		auto close = text.find('}', cursor + 1);
		if (close == std::string::npos) break;
		for (size_t pos = cursor + 1; pos < close;) {
			if (text[pos] != '\\') { ++pos; continue; }
			size_t start = pos++, name_start = pos;
			while (pos < close && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
			while (pos < close && std::isalpha(static_cast<unsigned char>(text[pos]))) ++pos;
			auto name = text.substr(name_start, pos - name_start);
			// \rStyleName is an ASS reset, not a tag named rStyleName.
			if (name.size() > 1 && name[0] == 'r' && name != "readmark" && name != "readtime" &&
				name.compare(0, 3, "rnd") != 0) { name = "r"; pos = name_start + 1; }
			size_t value_start = pos;
			if (pos < close && text[pos] == '(') {
				int depth = 1;
				for (++pos; pos < close && depth; ++pos) {
					if (text[pos] == '(') ++depth;
					else if (text[pos] == ')') --depth;
				}
			}
			else while (pos < close && text[pos] != '\\') ++pos;
			if (!name.empty()) result.push_back({name, text.substr(value_start, pos - value_start), start, pos, prefix});
		}
		cursor = close + 1;
	}
	return result;
}
std::array<std::string, ChatStyleTagCount> Values(ChatAppearance const& appearance) {
	return {appearance.panel.GetAssOverrideFormatted(), AlphaString(appearance.panel),
		appearance.automatic_title ? "" : appearance.title.GetAssOverrideFormatted(),
		appearance.automatic_header ? "" : appearance.header.GetAssOverrideFormatted(),
		SerializeChatBubbleTuple(appearance.left), SerializeChatBubbleTuple(appearance.right),
		std::to_string(std::max(0, std::min(2, appearance.read_mark))),
		std::to_string(std::max(0, appearance.read_time))};
}
ChatAppearance ExplicitAppearance(ChatAppearance appearance) {
	RefreshAutomaticChatHeader(appearance);
	appearance.automatic_title = appearance.automatic_header = false;
	return appearance;
}
} // namespace

ChatAppearance DefaultChatAppearance(agi::Color text, agi::Color name, agi::Color fill,
	agi::Color panel, double outline_size) {
	ChatAppearance result;
	result.panel = panel;
	result.left = result.right = {text, name, fill, fill, fill, 0, std::max(0.0, std::min(10000.0, outline_size))};
	RefreshAutomaticChatHeader(result);
	return result;
}
void RefreshAutomaticChatHeader(ChatAppearance& appearance) {
	bool dark = (appearance.panel.r * 299 + appearance.panel.g * 587 + appearance.panel.b * 114) / 1000 < 128;
	if (appearance.automatic_header) appearance.header = dark ? agi::Color(255, 255, 255) : agi::Color();
	if (appearance.automatic_title) appearance.title = dark ? agi::Color() : agi::Color(255, 255, 255);
}
bool ParseChatBubbleTuple(std::string const& tuple, ChatBubbleStyle& result) {
	auto value = Trim(tuple);
	if (value.size() < 2 || value.front() != '(' || value.back() != ')') return false;
	value = value.substr(1, value.size() - 2);
	std::vector<std::string> fields;
	size_t start = 0;
	for (;;) {
		auto comma = value.find(',', start);
		fields.push_back(value.substr(start, comma == std::string::npos ? comma : comma - start));
		if (comma == std::string::npos) break;
		start = comma + 1;
	}
	if (fields.size() != 12) return false;
	ChatBubbleStyle parsed;
	agi::Color* colors[] = {&parsed.text, &parsed.name, &parsed.fill, &parsed.border, &parsed.outline};
	int indices[] = {0, 2, 4, 6, 9};
	for (size_t i = 0; i < 5; ++i)
		if (!Color(fields[indices[i]], *colors[i]) || !Alpha(fields[indices[i] + 1], *colors[i])) return false;
	if (!Size(fields[8], parsed.border_size) || !Size(fields[11], parsed.outline_size)) return false;
	result = parsed;
	return true;
}
std::string SerializeChatBubbleTuple(ChatBubbleStyle const& style) {
	return "(" + style.text.GetAssOverrideFormatted() + "," + AlphaString(style.text) + "," +
		style.name.GetAssOverrideFormatted() + "," + AlphaString(style.name) + "," +
		style.fill.GetAssOverrideFormatted() + "," + AlphaString(style.fill) + "," +
		style.border.GetAssOverrideFormatted() + "," + AlphaString(style.border) + "," + SizeString(style.border_size) + "," +
		style.outline.GetAssOverrideFormatted() + "," + AlphaString(style.outline) + "," + SizeString(style.outline_size) + ")";
}

ChatStyleState LoadChatStyle(std::string const& text, ChatAppearance const& defaults) {
	ChatStyleState result;
	result.appearance = defaults;
	auto& a = result.appearance;
	auto tags = Tags(text);
	ChatBubbleStyle baseline = defaults.left;
	// Ordinary leading chat paint supplies defaults when a side tuple is absent.
	for (auto const& tag : tags) if (tag.prefix) {
		if (tag.name == "c" || tag.name == "1c") Color(tag.value, baseline.text);
		else if (tag.name == "1a") Alpha(tag.value, baseline.text);
		else if (tag.name == "2c") Color(tag.value, baseline.name);
		else if (tag.name == "2a") Alpha(tag.value, baseline.name);
		else if (tag.name == "3c" || tag.name == "bubc") Color(tag.value, baseline.fill);
		else if (tag.name == "3a" || tag.name == "buba") Alpha(tag.value, baseline.fill);
		else if (tag.name == "bubbc") Color(tag.value, baseline.border);
		else if (tag.name == "bubba") Alpha(tag.value, baseline.border);
		else if (tag.name == "bubbs") Size(tag.value, baseline.border_size);
		else if (tag.name == "bc") Color(tag.value, baseline.outline);
		else if (tag.name == "ba") Alpha(tag.value, baseline.outline);
		else if (tag.name == "bs" || tag.name == "bord") Size(tag.value, baseline.outline_size);
		else if (tag.name == "alpha") {
			Alpha(tag.value, baseline.text); Alpha(tag.value, baseline.name);
			Alpha(tag.value, baseline.fill); Alpha(tag.value, baseline.outline);
		}
	}
	a.left = a.right = baseline;
	for (auto const& tag : tags) {
		if (tag.prefix && tag.name == "alpha") Alpha(tag.value, a.panel);
		if (tag.name == "chatmode" && (Trim(tag.value) == "1" || Trim(tag.value) == "2" || Trim(tag.value) == "3"))
			result.has_chat_mode = true;
		// Explicit ordinary \2c is also a scene-wide header-name assignment.
		if (tag.name == "2c") {
			auto color = defaults.left.name;
			if (Trim(tag.value).empty() || Color(tag.value, color)) {
				a.title = color; a.title.a = 0; a.automatic_title = false;
				result.origin[Index(ChatStyleTag::Title)] = ChatValueOrigin::Line;
			}
		}
		for (size_t i = 0; i < names.size(); ++i) {
			if (tag.name != names[i] || (i < 2 && !tag.prefix)) continue;
			bool valid = false;
			unsigned long long number;
			switch (static_cast<ChatStyleTag>(i)) {
				case ChatStyleTag::PanelColor:
					valid = Trim(tag.value).empty() || Color(tag.value, a.panel);
					if (Trim(tag.value).empty()) { a.panel.r = defaults.panel.r; a.panel.g = defaults.panel.g; a.panel.b = defaults.panel.b; }
					break;
				case ChatStyleTag::PanelAlpha:
					valid = Trim(tag.value).empty() || Alpha(tag.value, a.panel);
					if (Trim(tag.value).empty()) a.panel.a = defaults.panel.a;
					break;
				case ChatStyleTag::Title:
					valid = Trim(tag.value).empty() || Color(tag.value, a.title);
					if (valid) { a.automatic_title = Trim(tag.value).empty(); a.title.a = 0; }
					break;
				case ChatStyleTag::Header:
					valid = Trim(tag.value).empty() || Color(tag.value, a.header);
					if (valid) { a.automatic_header = Trim(tag.value).empty(); a.header.a = 0; }
					break;
				case ChatStyleTag::Left:
				case ChatStyleTag::Right: {
					auto& side = i == Index(ChatStyleTag::Left) ? a.left : a.right;
					valid = ParseChatBubbleTuple(tag.value, side);
					if (Trim(tag.value) == "()") { side = baseline; valid = true; }
					break;
				}
				case ChatStyleTag::ReadMark:
					valid = Unsigned(tag.value, 2, number) && Trim(tag.value).find_first_not_of("0123456789") == std::string::npos;
					if (valid) a.read_mark = static_cast<int>(number);
					break;
				case ChatStyleTag::ReadTime:
					valid = Unsigned(tag.value, std::numeric_limits<int>::max(), number) && Trim(tag.value).find_first_not_of("0123456789") == std::string::npos;
					if (valid) a.read_time = static_cast<int>(number);
					break;
				default: break;
			}
			if (valid) { result.authored.set(i); result.origin[i] = ChatValueOrigin::Line; }
		}
	}
	RefreshAutomaticChatHeader(a);
	return result;
}

std::string SerializeChatAppearance(ChatAppearance const& appearance) {
	auto values = Values(appearance);
	std::string result;
	for (size_t i = 0; i < names.size(); ++i) result += "\\" + names[i] + values[i];
	return result;
}
void SwapChatSides(ChatAppearance& appearance) { std::swap(appearance.left, appearance.right); }

ChatStyleEdit ApplyChatStyle(std::string const& text, ChatAppearance const& appearance, ChatStyleMask const& changed) {
	ChatStyleEdit result{text, {}};
	if (changed.none()) return result;
	for (auto const& tag : Tags(text)) {
		for (size_t i = 0; i < names.size(); ++i)
			if (changed[i] && tag.name == names[i] && (i >= 2 || tag.prefix))
				result.edits.push_back({tag.start, tag.end, {}});
	}
	// Append to the first leading override block, after its unrelated tags, so
	// explicit header colors take precedence over a leading ordinary \2c.
	size_t insert = text.size() && text.front() == '{' ? text.find('}') : std::string::npos;
	bool new_block = insert == std::string::npos;
	if (new_block) insert = 0;
	auto values = Values(appearance);
	std::string replacement;
	for (size_t i = 0; i < names.size(); ++i)
		if (changed[i]) replacement += "\\" + names[i] + values[i];
	if (new_block) replacement = "{" + replacement + "}";
	result.edits.push_back({insert, insert, replacement});
	std::stable_sort(result.edits.begin(), result.edits.end(), [](ChatTextEdit const& a, ChatTextEdit const& b) {
		return a.start > b.start;
	});
	for (auto const& edit : result.edits) result.text.replace(edit.start, edit.end - edit.start, edit.replacement);
	return result;
}
int MapChatTextPosition(int position, ChatStyleEdit const& edit) {
	for (auto const& range : edit.edits)
		position = agi::ass::MoveTextPositionAfterEdit(position, static_cast<int>(range.start),
			static_cast<int>(range.end), static_cast<int>(range.replacement.size()));
	return position;
}

ChatAppearance const* ChatStylePresets::Find(std::string const& name) const {
	auto it = presets.find(name);
	return it == presets.end() ? nullptr : &it->second;
}
bool ChatStylePresets::Add(std::string const& name, ChatAppearance const& appearance) {
	if (Trim(name).empty()) return false;
	return presets.emplace(name, ExplicitAppearance(appearance)).second;
}
bool ChatStylePresets::Save(std::string const& name, ChatAppearance const& appearance) {
	auto it = presets.find(name);
	if (it == presets.end()) return false;
	it->second = ExplicitAppearance(appearance);
	return true;
}
bool ChatStylePresets::Rename(std::string const& old_name, std::string const& new_name) {
	if (old_name == new_name) return Find(old_name) != nullptr;
	if (Trim(new_name).empty() || Find(new_name)) return false;
	auto it = presets.find(old_name);
	if (it == presets.end()) return false;
	presets.emplace(new_name, it->second); presets.erase(it);
	return true;
}
bool ChatStylePresets::Delete(std::string const& name) { return presets.erase(name) != 0; }
std::string ChatStylePresets::Serialize() const {
	json::Object entries;
	for (auto const& preset : presets) entries[preset.first] = "{" + SerializeChatAppearance(preset.second) + "}";
	json::Object root;
	root["version"] = int64_t(1); root["presets"] = std::move(entries);
	std::ostringstream stream;
	agi::JsonWriter::Write(root, stream);
	return stream.str();
}
bool ChatStylePresets::Load(std::string const& data) {
	try {
		json::UnknownElement root;
		std::istringstream stream(data);
		json::Reader::Read(root, stream);
		auto const& object = static_cast<json::Object const&>(root);
		if (static_cast<json::Integer const&>(object.at("version")) != 1) return false;
		auto const& entries = static_cast<json::Object const&>(object.at("presets"));
		ChatStylePresets loaded;
		for (auto const& entry : entries) {
			auto state = LoadChatStyle(static_cast<json::String const&>(entry.second), DefaultChatAppearance());
			if (!state.authored.all() || !loaded.Add(entry.first, state.appearance)) return false;
		}
		presets = std::move(loaded.presets);
		return true;
	}
	catch (...) { return false; }
}
} // namespace mangetsu
