#include "../../src/audio_dialog_text.h"

#include <gtest/gtest.h>

using audio_dialog_text::Pieces;
using audio_dialog_text::SplitAtCaret;
using audio_dialog_text::Join;

TEST(AudioDialogText, SplitAtCaretAndJoinNeighbors) {
    Pieces split;
    ASSERT_TRUE(SplitAtCaret("hikari wo mirai he", 10, split));
    EXPECT_EQ("hikari wo", split.before);
    EXPECT_EQ("mirai he", split.after);
    EXPECT_EQ("mirai he sekai", Join(split.after, "sekai"));
    EXPECT_EQ("kagayaku hikari wo", Join("kagayaku", split.before));
}
TEST(AudioDialogText, EmptyAndBoundaryPositions) {
    Pieces split;
    ASSERT_TRUE(SplitAtCaret("abc", 0, split));
    EXPECT_TRUE(split.before.empty());
    EXPECT_EQ("abc", split.after);
    ASSERT_TRUE(SplitAtCaret("abc", 3, split));
    EXPECT_EQ("abc", split.before);
    EXPECT_TRUE(split.after.empty());
    EXPECT_FALSE(SplitAtCaret("abc", 4, split));
}
TEST(AudioDialogText, AssAndUnicodeNeverSplitMidToken) {
    Pieces split;
    std::string tagged = "{\\i1}光\\N未来";
    EXPECT_FALSE(SplitAtCaret(tagged, 3, split));
    EXPECT_FALSE(SplitAtCaret(tagged, 8, split)); // interior of multibyte UTF-8
    EXPECT_FALSE(SplitAtCaret(tagged, 12, split)); // between backslash and N
    ASSERT_TRUE(SplitAtCaret(tagged, 11, split));
    EXPECT_EQ("{\\i1}光", split.before);
    EXPECT_EQ("\\N未来", split.after);
}
TEST(AudioDialogText, PreserveRawTagsAcrossBoundary) {
    Pieces split;
    std::string text = "{\\c&H00FF00&}hikari wo {\\b1}mirai he";
    size_t caret = text.find("{\\b1}");
    ASSERT_NE(std::string::npos, caret);
    ASSERT_TRUE(SplitAtCaret(text, caret, split));
    EXPECT_EQ("{\\c&H00FF00&}hikari wo", split.before);
    EXPECT_EQ("{\\b1}mirai he", split.after);
}
