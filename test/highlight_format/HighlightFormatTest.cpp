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
