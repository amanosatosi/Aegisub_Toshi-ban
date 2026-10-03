// Mangetsu native-chat appearance, independent of the dialog and renderer.
#pragma once

#include <libaegisub/color.h>

#include <array>
#include <bitset>
#include <map>
#include <string>
#include <vector>

namespace mangetsu {

struct ChatBubbleStyle {
	agi::Color text, name, fill, border, outline;
	double border_size = 0, outline_size = 0;
};

enum class ChatStyleTag { PanelColor, PanelAlpha, Title, Header, Left, Right, ReadMark, ReadTime, Count };
constexpr size_t ChatStyleTagCount = static_cast<size_t>(ChatStyleTag::Count);
using ChatStyleMask = std::bitset<ChatStyleTagCount>;
enum class ChatValueOrigin { Default, Line, Edited };

struct ChatAppearance {
	agi::Color panel, title, header;
	ChatBubbleStyle left, right;
	int read_mark = 2, read_time = 0;
	bool automatic_title = true, automatic_header = true;
};

struct ChatStyleState {
	ChatAppearance appearance;
	std::array<ChatValueOrigin, ChatStyleTagCount> origin{};
	ChatStyleMask authored;
	bool has_chat_mode = false;
};

ChatAppearance DefaultChatAppearance(agi::Color text = {255, 255, 255},
	agi::Color name = {255, 0, 0}, agi::Color fill = {}, agi::Color panel = {},
	double outline_size = 2);
void RefreshAutomaticChatHeader(ChatAppearance& appearance);
bool ParseChatBubbleTuple(std::string const& tuple, ChatBubbleStyle& result);
std::string SerializeChatBubbleTuple(ChatBubbleStyle const& style);
ChatStyleState LoadChatStyle(std::string const& text, ChatAppearance const& defaults);
std::string SerializeChatAppearance(ChatAppearance const& appearance);
void SwapChatSides(ChatAppearance& appearance);

struct ChatTextEdit { size_t start, end; std::string replacement; };
struct ChatStyleEdit {
	std::string text;
	// Source-range edits in application order; map the active editor through these.
	std::vector<ChatTextEdit> edits;
};

/// Update only requested appearance properties. Nested transforms and message
/// content are opaque; ordinary inline ASS color/alpha tags remain untouched.
ChatStyleEdit ApplyChatStyle(std::string const& text, ChatAppearance const& appearance,
	ChatStyleMask const& changed);
int MapChatTextPosition(int position, ChatStyleEdit const& edit);

/// Config-only presets: the payload contains exclusively the eight appearance
/// tags, never contact/message contents, chat mode, animation or event timing.
class ChatStylePresets {
	std::map<std::string, ChatAppearance> presets;
public:
	std::map<std::string, ChatAppearance> const& Entries() const { return presets; }
	ChatAppearance const* Find(std::string const& name) const;
	bool Add(std::string const& name, ChatAppearance const& appearance);
	bool Save(std::string const& name, ChatAppearance const& appearance);
	bool Rename(std::string const& old_name, std::string const& new_name);
	bool Delete(std::string const& name);
	std::string Serialize() const;
	// Transactional: malformed config leaves the existing presets untouched.
	bool Load(std::string const& json);
};

} // namespace mangetsu
