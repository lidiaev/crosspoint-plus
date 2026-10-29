#include <gtest/gtest.h>

#include "activities/notes/NoteMarkdown.h"

using namespace note_md;

namespace {
// What the editor shows: hidden bytes dropped, glyphs as tokens, styled runs bracketed.
std::string shown(const std::string& text, size_t rawStart = std::string::npos) {
  std::vector<uint8_t> attrs;
  style(text, rawStart, attrs);
  std::string out;
  uint8_t open = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const uint8_t a = attrs[i];
    if (a & HIDDEN) continue;
    const uint8_t s = a & (STYLE_MASK | STRIKE);
    if (s != open) {
      if (open) out += "}";
      if (s) out += (s & STRIKE) ? "~{" : (s == BOLD ? "B{" : (s == ITALIC ? "I{" : "BI{"));
      open = s;
    }
    switch (glyphOf(a)) {
      case Glyph::Bullet:
        out += "•";
        continue;
      case Glyph::Box:
        out += "☐";
        continue;
      case Glyph::BoxChecked:
        out += "☑";
        continue;
      case Glyph::Quote:
        out += "▌";
        continue;
      case Glyph::Rule:
        out += "──";
        continue;
      case Glyph::CodeEdge:
        out += "|";
        continue;
      case Glyph::None:
        break;
    }
    out += text[i];
  }
  if (open) out += "}";
  return out;
}
}  // namespace

TEST(NoteMarkdown, PlainTextIsUntouched) { EXPECT_EQ(shown("Hello, world"), "Hello, world"); }

TEST(NoteMarkdown, HeadingHidesMarksAndIsBold) { EXPECT_EQ(shown("## Plan\nnext"), "B{Plan}\nnext"); }

TEST(NoteMarkdown, HashWithoutSpaceIsText) { EXPECT_EQ(shown("#tag"), "#tag"); }

TEST(NoteMarkdown, InlineStyles) {
  EXPECT_EQ(shown("a **b** c"), "a B{b} c");
  EXPECT_EQ(shown("a *b* c"), "a I{b} c");
  EXPECT_EQ(shown("a ~~b~~ c"), "a ~{b} c");
  EXPECT_EQ(shown("**a *b* c**"), "B{a }BI{b}B{ c}");
}

TEST(NoteMarkdown, UnderscoresInsideWordsStay) { EXPECT_EQ(shown("snake_case_name"), "snake_case_name"); }

TEST(NoteMarkdown, LoneStarsStay) {
  EXPECT_EQ(shown("2 * 3 * 4"), "2 * 3 * 4");
  EXPECT_EQ(shown("**"), "**");
}

TEST(NoteMarkdown, CodeIsLiteralAndFramed) {
  EXPECT_EQ(shown("x `a*b*c` y"), "x |a*b*c| y");
  EXPECT_EQ(shown("`a`", 0), "`a`");
}

TEST(NoteMarkdown, FencedCodeBlock) {
  const std::string t = "a\n```js\nx *y* # z\n```\nb";
  std::vector<uint8_t> attrs;
  std::vector<Block> blocks;
  style(t, std::string::npos, attrs, &blocks);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 2u);
  EXPECT_EQ(blocks[0].end, t.size() - 2);
  EXPECT_EQ(shown(t), "a\n\nx *y* # z\n\nb");
  EXPECT_EQ(shown(t, 2), "a\n```js\nx *y* # z\n\nb");
}

TEST(NoteMarkdown, UnclosedFenceRunsToTheEnd) {
  std::vector<uint8_t> attrs;
  std::vector<Block> blocks;
  style("```\ncode", std::string::npos, attrs, &blocks);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].end, 8u);
}

TEST(NoteMarkdown, HeadingLevel) {
  std::vector<uint8_t> attrs;
  const std::string t = "# A\n### B\n#C\n```\n# D\n```";
  style(t, std::string::npos, attrs);
  EXPECT_EQ(headingLevel(t, attrs, 0), 1);
  EXPECT_EQ(headingLevel(t, attrs, 4), 3);
  EXPECT_EQ(headingLevel(t, attrs, 10), 0);
  EXPECT_EQ(headingLevel(t, attrs, 17), 0);
}

TEST(NoteMarkdown, Lists) {
  EXPECT_EQ(shown("- milk"), "• milk");
  EXPECT_EQ(shown("- [ ] milk"), "☐milk");
  EXPECT_EQ(shown("- [x] milk"), "☑~{milk}");
  EXPECT_EQ(shown("  - nested"), "  • nested");
}

TEST(NoteMarkdown, QuoteAndRule) {
  EXPECT_EQ(shown("> said"), "▌said");
  EXPECT_EQ(shown("---"), "──");
}

TEST(NoteMarkdown, RawParagraphShowsMarkupButKeepsStyle) {
  EXPECT_EQ(shown("- [ ] a\n**b**", 8), "☐a\n**B{b}**");
  EXPECT_EQ(shown("- [ ] a\n**b**", 0), "- [ ] a\nB{b}");
}

TEST(NoteMarkdown, ListPrefixContinues) {
  const std::string t = "- [x] done";
  const ListPrefix p = listPrefix(t, 0, t.size());
  EXPECT_EQ(p.length, 6u);
  EXPECT_EQ(p.next, "- [ ] ");
  EXPECT_FALSE(p.empty);
  EXPECT_EQ(listPrefix("9. x", 0, 4).next, "10. ");
  EXPECT_EQ(listPrefix("  * x", 0, 5).next, "  * ");
  EXPECT_TRUE(listPrefix("- ", 0, 2).empty);
  EXPECT_EQ(listPrefix("plain", 0, 5).length, 0u);
}
