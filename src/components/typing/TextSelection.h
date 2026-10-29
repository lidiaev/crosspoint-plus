#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Selection helpers shared by the note editor and text fields. Offsets are byte
// offsets into UTF-8 text, always on code point boundaries.
namespace text_select {

struct Range {
  size_t start = 0;
  size_t end = 0;  // exclusive
  bool empty() const { return start >= end; }
};

// Letters and digits of any script, apostrophes and underscores.
bool isWordChar(uint32_t cp);

// The word at offset, or just before it when offset ends a word; otherwise the
// single character at offset. Empty on a line break or at the end of the text.
Range wordAt(const std::string& text, size_t offset);

// Code point boundaries next to offset.
size_t prevBoundary(const std::string& text, size_t offset);
size_t nextBoundary(const std::string& text, size_t offset);

// The selection that keeps [anchor.start, anchor.end) and reaches offset, as
// when a held finger drags away from the word it selected.
Range extend(const Range& anchor, size_t offset);

}  // namespace text_select

// One clipboard for the whole system; lives until reboot.
namespace clipboard {
void set(std::string text);
const std::string& get();
inline bool empty() { return get().empty(); }
}  // namespace clipboard
