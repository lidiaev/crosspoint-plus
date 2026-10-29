#include "TextSelection.h"

namespace text_select {
namespace {
bool isContinuation(const char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

uint32_t decodeAt(const std::string& s, const size_t i) {
  const uint8_t c = static_cast<uint8_t>(s[i]);
  if (c < 0x80) return c;
  if (c < 0xE0 && i + 1 < s.size()) return ((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F);
  if (c < 0xF0 && i + 2 < s.size()) {
    return ((c & 0x0F) << 12) | ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(s[i + 2]) & 0x3F);
  }
  return 0xFFFD;
}
}  // namespace

bool isWordChar(const uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' ||
           cp == '\'';
  }
  // General punctuation, guillemets, the no-break space and symbols are not part of a word.
  if (cp == 0x00A0 || cp == 0x00AB || cp == 0x00BB || (cp >= 0x2000 && cp <= 0x206F)) return cp == 0x2019;
  if (cp >= 0x00A1 && cp <= 0x00BF) return false;
  return cp != 0xFFFD;
}

size_t prevBoundary(const std::string& text, size_t offset) {
  if (offset == 0) return 0;
  --offset;
  while (offset > 0 && isContinuation(text[offset])) --offset;
  return offset;
}

size_t nextBoundary(const std::string& text, size_t offset) {
  if (offset >= text.size()) return text.size();
  ++offset;
  while (offset < text.size() && isContinuation(text[offset])) ++offset;
  return offset;
}

Range wordAt(const std::string& text, const size_t offset) {
  size_t at = offset > text.size() ? text.size() : offset;
  const bool here = at < text.size() && isWordChar(decodeAt(text, at));
  if (!here) {
    const size_t before = prevBoundary(text, at);
    if (at > 0 && isWordChar(decodeAt(text, before))) {
      at = before;
    } else {
      if (at >= text.size() || text[at] == '\n') return {at, at};
      return {at, nextBoundary(text, at)};
    }
  }
  size_t start = at;
  while (start > 0) {
    const size_t before = prevBoundary(text, start);
    if (!isWordChar(decodeAt(text, before))) break;
    start = before;
  }
  size_t end = at;
  while (end < text.size() && isWordChar(decodeAt(text, end))) end = nextBoundary(text, end);
  return {start, end};
}

Range extend(const Range& anchor, const size_t offset) {
  if (offset < anchor.start) return {offset, anchor.end};
  if (offset > anchor.end) return {anchor.start, offset};
  return anchor;
}

}  // namespace text_select

namespace clipboard {
namespace {
std::string& storage() {
  static std::string text;
  return text;
}
}  // namespace

void set(std::string text) { storage() = std::move(text); }
const std::string& get() { return storage(); }
}  // namespace clipboard
