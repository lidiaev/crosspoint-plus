#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Markdown for the note editor, rendered in place: every byte of the note gets
// display attributes, and markup is hidden or drawn as a glyph instead of being
// rewritten, so offsets stay byte offsets into the source text.
namespace note_md {

inline constexpr uint8_t STYLE_MASK = 0x03;  // EpdFontFamily::Style: regular, bold, italic, bold italic
inline constexpr uint8_t BOLD = 0x01;
inline constexpr uint8_t ITALIC = 0x02;
inline constexpr uint8_t HIDDEN = 0x04;  // markup: zero width, not drawn
inline constexpr uint8_t GLYPH_SHIFT = 3;
inline constexpr uint8_t GLYPH_MASK = 0x38;
inline constexpr uint8_t STRIKE = 0x40;
inline constexpr uint8_t CODE = 0x80;  // inside `code`: no inline markup

// A list or block marker drawn in place of its first byte.
// CodeEdge is a backtick drawn as the padding of the frame around `code`.
enum class Glyph : uint8_t { None, Bullet, Box, BoxChecked, Quote, Rule, CodeEdge };

inline Glyph glyphOf(const uint8_t attr) { return static_cast<Glyph>((attr & GLYPH_MASK) >> GLYPH_SHIFT); }

// Fills attrs with one entry per byte of text. The paragraph starting at
// rawStart (the one being edited) keeps its markup visible; pass npos for none.
void style(const std::string& text, size_t rawStart, std::vector<uint8_t>& attrs);

// 1-6 for a heading paragraph starting at `start`, 0 otherwise.
int headingLevel(const std::string& text, const std::vector<uint8_t>& attrs, size_t start);

// The list marker that opens the paragraph [start, end), if any.
struct ListPrefix {
  size_t length = 0;   // bytes of indent and marker; 0 = not a list item
  std::string next;    // marker for the item after it ("- ", "- [ ] ", "3. ")
  bool empty = false;  // nothing after the marker
};
ListPrefix listPrefix(const std::string& text, size_t start, size_t end);

// The marker that opens the paragraph [start, end): its kind, where it starts
// (after the indent) and its length with the space after it.
enum class Block : uint8_t { Plain, Bullet, Numbered, Checkbox, Heading1, Heading2, Heading };
struct BlockMarker {
  Block kind = Block::Plain;
  size_t at = 0;
  size_t length = 0;
  unsigned number = 0;  // for Numbered
};
BlockMarker blockMarker(const std::string& text, size_t start, size_t end);

// Emphasis around [start, end): the count of matching '*' on both sides, 0-3
// (1 italic, 2 bold, 3 bold italic).
int emphasisAround(const std::string& text, size_t start, size_t end);

// The text as shown, without markup: list bullets stay as "•", other glyphs go.
std::string plainText(const std::string& text);

// For a checkbox glyph at offset i, the offset of its mark (' ' or 'x').
inline size_t checkboxMark(const size_t i) { return i + 3; }

}  // namespace note_md
