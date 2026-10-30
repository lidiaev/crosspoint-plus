#include <gtest/gtest.h>

#include "activities/reader/HighlightFormat.h"

using highlights::Highlight;

TEST(HighlightFormat, RoundTrip) {
  Highlight first;
  first.spine = 4;
  first.start = 1200;
  first.end = 1260;
  first.percent = 37;
  first.quote = "Цитата из книги.";
  first.comment = "Первая строка\n\nВторая строка";
  Highlight second;
  second.spine = 7;
  second.start = 10;
  second.end = 20;
  second.quote = "No comment";

  std::string title;
  std::vector<Highlight> parsed;
  highlights::parse(highlights::format("Книга", {first, second}), title, parsed);
  EXPECT_EQ(title, "Книга");
  ASSERT_EQ(parsed.size(), 2u);
  EXPECT_EQ(parsed[0].spine, 4);
  EXPECT_EQ(parsed[0].start, 1200u);
  EXPECT_EQ(parsed[0].end, 1260u);
  EXPECT_EQ(parsed[0].percent, 37);
  EXPECT_EQ(parsed[0].quote, first.quote);
  EXPECT_EQ(parsed[0].comment, first.comment);
  EXPECT_EQ(parsed[1].quote, "No comment");
  EXPECT_TRUE(parsed[1].comment.empty());
}

TEST(HighlightFormat, QuoteStaysOnOneLine) {
  Highlight entry;
  entry.spine = 0;
  entry.end = 3;
  entry.quote = "a\nb";
  std::string title;
  std::vector<Highlight> parsed;
  highlights::parse(highlights::format("T", {entry}), title, parsed);
  ASSERT_EQ(parsed.size(), 1u);
  EXPECT_EQ(parsed[0].quote, "a b");
}

TEST(HighlightFormat, HandWrittenEntryKeepsItsTextWithoutAnchor) {
  std::string title;
  std::vector<Highlight> parsed;
  highlights::parse("# T\n\n> Added by hand\n\nAnd a comment\n", title, parsed);
  ASSERT_EQ(parsed.size(), 1u);
  EXPECT_EQ(parsed[0].spine, -1);
  EXPECT_EQ(parsed[0].comment, "And a comment");

  std::vector<Highlight> again;
  highlights::parse(highlights::format(title, parsed), title, again);
  ASSERT_EQ(again.size(), 1u);
  EXPECT_EQ(again[0].quote, "Added by hand");
}

TEST(HighlightFormat, SortsByBookOrderWithHandWrittenLast) {
  std::vector<Highlight> entries(3);
  entries[0].spine = -1;
  entries[1].spine = 5;
  entries[1].start = 9;
  entries[2].spine = 5;
  entries[2].start = 2;
  highlights::sortByPosition(entries);
  EXPECT_EQ(entries[0].start, 2u);
  EXPECT_EQ(entries[1].start, 9u);
  EXPECT_EQ(entries[2].spine, -1);
}

namespace {
Highlight make(const int spine, const uint32_t start, const uint32_t end, const std::string& quote,
               const std::string& comment = "") {
  Highlight entry;
  entry.spine = spine;
  entry.start = start;
  entry.end = end;
  entry.quote = quote;
  entry.comment = comment;
  return entry;
}
}  // namespace

TEST(HighlightFormat, SameRangeBecomesOneEntryWithTheComment) {
  std::vector<Highlight> entries{make(1, 10, 15, "слово"), make(1, 10, 15, "слово", "мысль")};
  highlights::mergeOverlapping(entries);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].quote, "слово");
  EXPECT_EQ(entries[0].comment, "мысль");
}

TEST(HighlightFormat, WiderSelectionSwallowsTheHighlightInside) {
  std::vector<Highlight> entries{make(1, 14, 19, "слово", "мысль"), make(1, 10, 30, "вокруг слово вокруг")};
  highlights::mergeOverlapping(entries);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].start, 10u);
  EXPECT_EQ(entries[0].end, 30u);
  EXPECT_EQ(entries[0].quote, "вокруг слово вокруг");
  EXPECT_EQ(entries[0].comment, "мысль");
}

TEST(HighlightFormat, PartialOverlapJoinsQuotesAndComments) {
  std::vector<Highlight> entries{make(1, 0, 6, "abc de", "one"), make(1, 4, 10, "de fgh", "two")};
  highlights::mergeOverlapping(entries);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].end, 10u);
  EXPECT_EQ(entries[0].quote, "abc de fgh");
  EXPECT_EQ(entries[0].comment, "one\n\ntwo");
}

TEST(HighlightFormat, SeparateHighlightsStaySeparate) {
  std::vector<Highlight> entries{make(1, 0, 5, "a"), make(1, 5, 9, "b"), make(2, 0, 5, "c"), make(-1, 0, 0, "d"),
                                 make(-1, 0, 0, "e")};
  highlights::mergeOverlapping(entries);
  EXPECT_EQ(entries.size(), 5u);
}
