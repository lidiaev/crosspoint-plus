#include <gtest/gtest.h>

#include <string>

#include "components/typing/TextSelection.h"

using text_select::Range;
using text_select::wordAt;

namespace {
std::string slice(const std::string& text, const Range& r) { return text.substr(r.start, r.end - r.start); }
}  // namespace

TEST(TextSelection, WordInsideAndAtEdges) {
  const std::string text = "hello world";
  EXPECT_EQ(slice(text, wordAt(text, 2)), "hello");
  EXPECT_EQ(slice(text, wordAt(text, 0)), "hello");
  EXPECT_EQ(slice(text, wordAt(text, 5)), "hello");  // just after the word
  EXPECT_EQ(slice(text, wordAt(text, 6)), "world");
  EXPECT_EQ(slice(text, wordAt(text, 11)), "world");  // end of text
}

TEST(TextSelection, CyrillicWord) {
  const std::string text = "Привет, мир!";
  EXPECT_EQ(slice(text, wordAt(text, 4)), "Привет");
  EXPECT_EQ(slice(text, wordAt(text, text.find("мир"))), "мир");
}

TEST(TextSelection, PunctuationStopsWords) {
  const std::string text = "**bold** «цитата» a—b";
  EXPECT_EQ(slice(text, wordAt(text, 3)), "bold");
  EXPECT_EQ(slice(text, wordAt(text, text.find("цитата") + 2)), "цитата");
  EXPECT_EQ(slice(text, wordAt(text, text.find('a'))), "a");
}

TEST(TextSelection, ApostropheKeepsWord) {
  const std::string text = "don't stop";
  EXPECT_EQ(slice(text, wordAt(text, 1)), "don't");
}

TEST(TextSelection, NonWordSelectsOneCharacter) {
  const std::string text = "a  b";
  EXPECT_EQ(slice(text, wordAt(text, 2)), " ");
}

TEST(TextSelection, LineBreakAndEmptyTextAreEmpty) {
  EXPECT_TRUE(wordAt("", 0).empty());
  const std::string text = "x\n\ny";
  EXPECT_TRUE(wordAt(text, 2).empty());
}

TEST(TextSelection, Boundaries) {
  const std::string text = "aжb";
  EXPECT_EQ(text_select::nextBoundary(text, 1), 3u);
  EXPECT_EQ(text_select::prevBoundary(text, 3), 1u);
  EXPECT_EQ(text_select::prevBoundary(text, 0), 0u);
  EXPECT_EQ(text_select::nextBoundary(text, text.size()), text.size());
}

TEST(TextSelection, ExtendKeepsAnchorWord) {
  const Range anchor{10, 15};
  EXPECT_EQ(text_select::extend(anchor, 20).end, 20u);
  EXPECT_EQ(text_select::extend(anchor, 20).start, 10u);
  EXPECT_EQ(text_select::extend(anchor, 3).start, 3u);
  EXPECT_EQ(text_select::extend(anchor, 3).end, 15u);
  EXPECT_EQ(text_select::extend(anchor, 12).start, 10u);
}

TEST(TextSelection, ClipboardKeepsText) {
  EXPECT_TRUE(clipboard::empty());
  clipboard::set("copied");
  EXPECT_EQ(clipboard::get(), "copied");
}
