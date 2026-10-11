// Text operations for Shift+drag dialogue timing.
// Work with raw ASS UTF-8 so override blocks are never split or decoded.
#pragma once

#include <cstddef>
#include <string>

namespace audio_dialog_text {

struct Pieces {
    std::string before;
    std::string after;
};

inline bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline void TrimRight(std::string &text) {
    while (!text.empty() && IsSpace(text.back()))
        text.pop_back();
}

inline void TrimLeft(std::string &text) {
    auto pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos)
        text.clear();
    else if (pos)
        text.erase(0, pos);
}

// A caret is a byte offset supplied by Scintilla. Do not split inside UTF-8,
// inside an ASS override block, or in the middle of a literal escape.
inline bool IsSafeBoundary(std::string const& text, size_t pos) {
    if (pos > text.size())
        return false;
    if (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xc0) == 0x80)
        return false;

    bool in_block = false;
    for (size_t i = 0; i < pos; ++i) {
        if (text[i] == '{')
            in_block = true;
        else if (text[i] == '}')
            in_block = false;
    }
    if (in_block)
        return false;

    if (pos && pos < text.size() && text[pos - 1] == '\\') {
        char c = text[pos];
        if (c == 'N' || c == 'n' || c == 'h' || c == '{' || c == '}')
            return false;
    }
    return true;
}

inline bool SplitAtCaret(std::string const& text, size_t pos, Pieces &out) {
    if (!IsSafeBoundary(text, pos))
        return false;
    Pieces result{text.substr(0, pos), text.substr(pos)};
    TrimRight(result.before);
    TrimLeft(result.after);
    out = std::move(result);
    return true;
}

inline std::string Join(std::string const& before, std::string const& after) {
    if (before.empty())
        return after;
    if (after.empty())
        return before;

    // Neither the neighbor's content nor the transferred text is discarded.
    // Use a single word separator at the join, except at ASS hard line breaks.
    bool hard_break = (before.size() >= 2 && before.compare(before.size() - 2, 2, "\\N") == 0) ||
                      (after.size() >= 2 && after.compare(0, 2, "\\N") == 0);
    if (IsSpace(before.back()) || IsSpace(after.front()) || hard_break)
        return before + after;
    return before + " " + after;
}

} // namespace audio_dialog_text
