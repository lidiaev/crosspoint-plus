#include "NoteMarkdown.h"

#include <algorithm>

namespace note_md {
namespace {

bool isWordByte(const char c) {
  const auto u = static_cast<uint8_t>(c);
  return u >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void setGlyph(std::vector<uint8_t>& attrs, const size_t i, const Glyph glyph) {
  attrs[i] = static_cast<uint8_t>((attrs[i] & ~GLYPH_MASK) | (static_cast<uint8_t>(glyph) << GLYPH_SHIFT));
}

void hide(std::vector<uint8_t>& attrs, const size_t from, const size_t to) {
  for (size_t i = from; i < to; ++i) attrs[i] |= HIDDEN;
}

void apply(std::vector<uint8_t>& attrs, const size_t from, const size_t to, const uint8_t bits) {
  for (size_t i = from; i < to; ++i) attrs[i] |= bits;
}

bool plain(const std::vector<uint8_t>& attrs, const size_t i) { return (attrs[i] & (HIDDEN | CODE)) == 0; }

// Inline markup in [from, to): `code`, **bold**, __bold__, ~~strike~~, *italic*, _italic_.
void inlineSpans(const std::string& text, const size_t from, const size_t to, std::vector<uint8_t>& attrs) {
  for (size_t i = from; i < to; ++i) {
    if (text[i] != '`') continue;
    const size_t close = text.find('`', i + 1);
    if (close == std::string::npos || close >= to) break;
    apply(attrs, i, close + 1, CODE);
    setGlyph(attrs, i, Glyph::CodeEdge);
    setGlyph(attrs, close, Glyph::CodeEdge);
    i = close;
  }

  static constexpr const char* PAIRS[] = {"**", "__", "~~"};
  for (size_t i = from; i + 1 < to; ++i) {
    for (const char* pair : PAIRS) {
      if (!plain(attrs, i) || text.compare(i, 2, pair) != 0) continue;
      size_t close = i + 2;
      while (close + 1 < to && !(plain(attrs, close) && text.compare(close, 2, pair) == 0)) ++close;
      if (close + 1 >= to || close == i + 2) continue;
      hide(attrs, i, i + 2);
      hide(attrs, close, close + 2);
      apply(attrs, i + 2, close, pair[0] == '~' ? STRIKE : BOLD);
      i = close + 1;
      break;
    }
  }

  for (size_t i = from; i + 1 < to; ++i) {
    const char c = text[i];
    if ((c != '*' && c != '_') || !plain(attrs, i)) continue;
    const char after = text[i + 1];
    if (after == ' ' || after == c) continue;
    if (c == '_' && i > from && isWordByte(text[i - 1])) continue;
    size_t close = i + 1;
    while (close < to && !(text[close] == c && plain(attrs, close) && text[close - 1] != ' ' &&
                           (c != '_' || close + 1 >= to || !isWordByte(text[close + 1])))) {
      ++close;
    }
    if (close >= to || close == i + 1) continue;
    hide(attrs, i, i + 1);
    hide(attrs, close, close + 1);
    apply(attrs, i + 1, close, ITALIC);
    i = close;
  }
}

bool isRule(const std::string& text, size_t i, const size_t end) {
  const char c = text[i];
  if (c != '-' && c != '*' && c != '_') return false;
  int count = 0;
  for (; i < end; ++i) {
    if (text[i] == c) {
      ++count;
    } else if (text[i] != ' ') {
      return false;
    }
  }
  return count >= 3;
}

bool isBullet(const std::string& text, const size_t i, const size_t end) {
  return i + 1 < end && (text[i] == '-' || text[i] == '*' || text[i] == '+') && text[i + 1] == ' ';
}

bool isCheckbox(const std::string& text, const size_t i, const size_t end) {
  return i + 4 < end && isBullet(text, i, end) && text[i + 2] == '[' &&
         (text[i + 3] == ' ' || text[i + 3] == 'x' || text[i + 3] == 'X') && text[i + 4] == ']' &&
         (i + 5 == end || text[i + 5] == ' ');
}

void paragraph(const std::string& text, const size_t start, const size_t end, std::vector<uint8_t>& attrs) {
  size_t i = start;
  while (i < end && text[i] == ' ') ++i;
  if (i == end) return;

  if (text[i] == '#') {
    size_t level = 0;
    while (i + level < end && text[i + level] == '#' && level < 6) ++level;
    if (i + level < end && text[i + level] == ' ') {
      hide(attrs, i, i + level + 1);
      apply(attrs, i + level + 1, end, BOLD);
      inlineSpans(text, i + level + 1, end, attrs);
      return;
    }
  }
  if (isRule(text, i, end)) {
    setGlyph(attrs, i, Glyph::Rule);
    hide(attrs, i + 1, end);
    return;
  }
  if (isCheckbox(text, i, end)) {
    setGlyph(attrs, i, text[i + 3] == ' ' ? Glyph::Box : Glyph::BoxChecked);
    const size_t content = std::min(i + 6, end);
    hide(attrs, i + 1, content);
    inlineSpans(text, content, end, attrs);
    // A done task is struck through.
    if (text[i + 3] != ' ') apply(attrs, content, end, STRIKE);
    return;
  }
  if (isBullet(text, i, end)) {
    setGlyph(attrs, i, Glyph::Bullet);
    inlineSpans(text, i + 1, end, attrs);
    return;
  }
  if (text[i] == '>') {
    setGlyph(attrs, i, Glyph::Quote);
    const size_t content = i + 1 < end && text[i + 1] == ' ' ? i + 2 : i + 1;
    hide(attrs, i + 1, content);
    inlineSpans(text, content, end, attrs);
    return;
  }
  inlineSpans(text, i, end, attrs);
}

}  // namespace

void style(const std::string& text, const size_t rawStart, std::vector<uint8_t>& attrs) {
  attrs.assign(text.size(), 0);
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    paragraph(text, start, end, attrs);
    if (start == rawStart) {
      // The paragraph being edited shows its markup; styles still apply.
      for (size_t i = start; i < end; ++i) attrs[i] &= static_cast<uint8_t>(~(HIDDEN | GLYPH_MASK));
    }
    start = end + 1;
  }
}

std::string plainText(const std::string& text) {
  std::vector<uint8_t> attrs;
  style(text, std::string::npos, attrs);
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const uint8_t a = attrs[i];
    if (a & HIDDEN) continue;
    const Glyph glyph = glyphOf(a);
    if (glyph == Glyph::Bullet) {
      out += "•";
    } else if (glyph == Glyph::None) {
      out += text[i];
    }
  }
  return out;
}

int headingLevel(const std::string& text, const std::vector<uint8_t>& attrs, const size_t start) {
  if (start >= text.size() || (attrs[start] & CODE)) return 0;
  size_t i = start;
  while (i < text.size() && text[i] == ' ') ++i;
  int level = 0;
  while (i < text.size() && text[i] == '#' && level < 6) {
    ++level;
    ++i;
  }
  return i < text.size() && text[i] == ' ' ? level : 0;
}

BlockMarker blockMarker(const std::string& text, const size_t start, const size_t end) {
  BlockMarker marker;
  size_t i = start;
  while (i < end && text[i] == ' ') ++i;
  marker.at = i;
  if (isCheckbox(text, i, end)) {
    marker.kind = Block::Checkbox;
    marker.length = i + 5 < end ? 6 : 5;
    return marker;
  }
  if (isBullet(text, i, end)) {
    marker.kind = Block::Bullet;
    marker.length = 2;
    return marker;
  }
  int level = 0;
  while (i + level < end && text[i + level] == '#' && level < 6) ++level;
  if (level > 0 && i + level < end && text[i + level] == ' ') {
    marker.kind = level == 1 ? Block::Heading1 : level == 2 ? Block::Heading2 : Block::Heading;
    marker.length = level + 1;
    return marker;
  }
  size_t d = i;
  unsigned number = 0;
  while (d < end && d - i < 4 && text[d] >= '0' && text[d] <= '9') number = number * 10 + (text[d++] - '0');
  if (d > i && d + 1 < end && text[d] == '.' && text[d + 1] == ' ') {
    marker.kind = Block::Numbered;
    marker.length = d + 2 - i;
    marker.number = number;
  }
  return marker;
}

int emphasisAround(const std::string& text, const size_t start, const size_t end) {
  int count = 0;
  while (count < 3 && start >= static_cast<size_t>(count) + 1 && end + count < text.size() &&
         text[start - count - 1] == '*' && text[end + count] == '*') {
    ++count;
  }
  return count;
}

ListPrefix listPrefix(const std::string& text, const size_t start, const size_t end) {
  ListPrefix prefix;
  size_t i = start;
  while (i < end && text[i] == ' ') ++i;
  const std::string indent(text, start, i - start);
  if (isCheckbox(text, i, end)) {
    prefix.length = std::min(i + 6, end) - start;
    prefix.next = indent + text[i] + " [ ] ";
  } else if (isBullet(text, i, end)) {
    prefix.length = i + 2 - start;
    prefix.next = indent + text[i] + " ";
  } else if (i < end && text[i] == '>') {
    prefix.length = (i + 1 < end && text[i + 1] == ' ' ? i + 2 : i + 1) - start;
    prefix.next = indent + "> ";
  } else {
    size_t d = i;
    unsigned number = 0;
    while (d < end && d - i < 4 && text[d] >= '0' && text[d] <= '9') number = number * 10 + (text[d++] - '0');
    if (d == i || d + 1 >= end || text[d] != '.' || text[d + 1] != ' ') return prefix;
    prefix.length = d + 2 - start;
    prefix.next = indent + std::to_string(number + 1) + ". ";
  }
  size_t rest = start + prefix.length;
  while (rest < end && text[rest] == ' ') ++rest;
  prefix.empty = rest == end;
  return prefix;
}

}  // namespace note_md
