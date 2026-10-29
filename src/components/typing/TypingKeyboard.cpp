#include "TypingKeyboard.h"

#include <GfxRenderer.h>

#include <cstring>

#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_KEY = 1;
constexpr int KEY_HEIGHT = 60;
constexpr int ROW_GAP = 8;
constexpr int GAP = 6;
constexpr int SIDE = 4;
constexpr int RADIUS = 7;
constexpr uint32_t DOUBLE_TAP_MS = 400;

// Key rows: UTF-8 characters separated by spaces. Key ids are the code points.
constexpr const char* RU_LOWER[3] = {"й ц у к е н г ш щ з х ъ", "ф ы в а п р о л д ж э", "я ч с м и т ь б ю"};
constexpr const char* RU_UPPER[3] = {"Й Ц У К Е Н Г Ш Щ З Х Ъ", "Ф Ы В А П Р О Л Д Ж Э", "Я Ч С М И Т Ь Б Ю"};
constexpr const char* EN_LOWER[3] = {"q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m"};
constexpr const char* EN_UPPER[3] = {"Q W E R T Y U I O P", "A S D F G H J K L", "Z X C V B N M"};
// iPhone-like symbol pages. Only characters the UI font has: it is also the note font.
constexpr const char* SYMBOLS[3] = {"1 2 3 4 5 6 7 8 9 0", "- / : ; ( ) « » @ \"", "— & ? ! ' …"};
constexpr const char* SYMBOLS_MORE[3] = {"[ ] { } # % ^ * + =", "_ \\ | ~ < > € $ £ •", "` ° § × ≈ ≠"};
constexpr int16_t KEY_SYMBOL_PAGE = -10;  // "#+=" / "123" on the symbol pages

int decodeUtf8(const char* s, int16_t& cp) {
  const uint8_t c = static_cast<uint8_t>(s[0]);
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  if (c < 0xE0) {
    cp = static_cast<int16_t>(((c & 0x1F) << 6) | (static_cast<uint8_t>(s[1]) & 0x3F));
    return 2;
  }
  cp = static_cast<int16_t>(((c & 0x0F) << 12) | ((static_cast<uint8_t>(s[1]) & 0x3F) << 6) |
                            (static_cast<uint8_t>(s[2]) & 0x3F));
  return 3;
}

void encodeUtf8(const uint16_t cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    out[1] = '\0';
  } else if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    out[2] = '\0';
  } else {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    out[3] = '\0';
  }
}

// Control-key glyphs: one 2 px outline style, centered on (cx, cy).
void drawShiftGlyph(const GfxRenderer& r, int cx, int cy, bool on, bool caps, bool ink) {
  const int xs[7] = {cx, cx + 10, cx + 5, cx + 5, cx - 5, cx - 5, cx - 10};
  const int ys[7] = {cy - 11, cy - 1, cy - 1, cy + 8, cy + 8, cy - 1, cy - 1};
  if (on) {
    r.fillPolygon(xs, ys, 7, ink);
  } else {
    for (int i = 0; i < 7; ++i) r.drawLine(xs[i], ys[i], xs[(i + 1) % 7], ys[(i + 1) % 7], 2, ink);
  }
  if (caps) r.fillRect(cx - 5, cy + 11, 11, 2, ink);
}

void drawDeleteGlyph(const GfxRenderer& r, int cx, int cy, bool ink) {
  const int xs[5] = {cx - 13, cx - 5, cx + 12, cx + 12, cx - 5};
  const int ys[5] = {cy, cy - 9, cy - 9, cy + 9, cy + 9};
  for (int i = 0; i < 5; ++i) r.drawLine(xs[i], ys[i], xs[(i + 1) % 5], ys[(i + 1) % 5], 2, ink);
  r.drawLine(cx - 1, cy - 4, cx + 7, cy + 4, 2, ink);
  r.drawLine(cx - 1, cy + 4, cx + 7, cy - 4, 2, ink);
}

void drawReturnGlyph(const GfxRenderer& r, int cx, int cy, bool ink) {
  r.drawLine(cx + 10, cy - 8, cx + 10, cy + 3, 2, ink);
  r.drawLine(cx + 10, cy + 3, cx - 10, cy + 3, 2, ink);
  r.drawLine(cx - 10, cy + 3, cx - 4, cy - 3, 2, ink);
  r.drawLine(cx - 10, cy + 3, cx - 4, cy + 9, 2, ink);
}

void drawLabel(const GfxRenderer& r, int font, int cx, int cy, const char* label, bool ink) {
  if (!label || !*label) return;
  const int tw = r.getTextWidth(font, label);
  const int th = r.getLineHeight(font);
  r.drawText(font, cx - tw / 2, cy - th / 2 - 2, label, ink);
}
}  // namespace

void TypingKeyboard::begin(const bool englishLayout) {
  english = englishLayout;
  shifted = false;
  capsLock = false;
  symbols = false;
  build();
}

int TypingKeyboard::height() { return ROWS * KEY_HEIGHT + (ROWS - 1) * ROW_GAP; }

fui::Rect TypingKeyboard::rect(const GfxRenderer& renderer) const {
  const int h = height();
  return fui::Rect{SIDE, static_cast<int16_t>(renderer.getScreenHeight() - SIDE - h),
                   static_cast<int16_t>(renderer.getScreenWidth() - 2 * SIDE), static_cast<int16_t>(h)};
}

// Shift and Backspace flank the third row; the bottom row has the same widths in
// every layer, so switching language or symbols never moves it.
void TypingKeyboard::build() {
  const bool upper = shifted || capsLock;
  const char* const* src = symbols   ? (symbolsMore ? SYMBOLS_MORE : SYMBOLS)
                           : english ? (upper ? EN_UPPER : EN_LOWER)
                                     : (upper ? RU_UPPER : RU_LOWER);
  for (uint8_t r = 0; r < 3; ++r) {
    uint8_t n = 0;
    if (r == 2) {
      keys[r][n++] =
          symbols ? fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Normal, fui::StateNormal, KEY_SYMBOL_PAGE, 3, true}
                  : fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Shift, fui::StateNormal, fui::QWERTY_KEY_SHIFT,
                                     3,       true};
    }
    for (const char* p = src[r]; *p && n < COLS - 1;) {
      if (*p == ' ') {
        ++p;
        continue;
      }
      int16_t cp = 0;
      const int len = decodeUtf8(p, cp);
      char* g = glyphs[r][n];
      memcpy(g, p, len);
      g[len] = '\0';
      keys[r][n++] = fui::KeyboardKey{g, g, fui::KeyKind::Normal, fui::StateNormal, cp, 2, true};
      p += len;
    }
    if (r == 2) {
      keys[r][n++] = fui::KeyboardKey{
          nullptr, nullptr, fui::KeyKind::Delete, fui::StateNormal, fui::QWERTY_KEY_BACKSPACE, 3, true};
    }
    rows[r] = fui::KeyboardRow{keys[r], n, static_cast<uint8_t>(r == 1 && !symbols ? 1 : 0)};
  }
  uint8_t n = 0;
  keys[3][n++] =
      fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Mode, fui::StateNormal, fui::QWERTY_KEY_MODE, 3, true};
  keys[3][n++] =
      fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Lang, fui::StateNormal, fui::QWERTY_KEY_LANG, 3, true};
  keys[3][n++] = fui::KeyboardKey{",", ",", fui::KeyKind::Normal, fui::StateNormal, ',', 2, true};
  keys[3][n++] = fui::KeyboardKey{nullptr, " ", fui::KeyKind::Space, fui::StateNormal, fui::QWERTY_KEY_SPACE, 9, true};
  keys[3][n++] = fui::KeyboardKey{".", ".", fui::KeyKind::Normal, fui::StateNormal, '.', 2, true};
  keys[3][n++] = fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Ok, fui::StateNormal, fui::QWERTY_KEY_ENTER, 5, true};
  rows[3] = fui::KeyboardRow{keys[3], n, 0};
  layout = fui::KeyboardLayout{rows, ROWS};
}

TypingKeyboard::Action TypingKeyboard::press(const int16_t value, const uint32_t downMs, char out[4]) {
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      if (capsLock) {
        capsLock = false;
        shifted = false;
      } else if (shifted && downMs - lastShiftAt < DOUBLE_TAP_MS) {
        capsLock = true;
      } else {
        shifted = !shifted;
      }
      lastShiftAt = downMs;
      build();
      return Action::LayoutChanged;
    case fui::QWERTY_KEY_MODE:
      symbols = !symbols;
      symbolsMore = false;
      shifted = false;
      build();
      return Action::LayoutChanged;
    case KEY_SYMBOL_PAGE:
      symbolsMore = !symbolsMore;
      build();
      return Action::LayoutChanged;
    case fui::QWERTY_KEY_LANG:
      english = !english;
      symbols = false;
      symbolsMore = false;
      build();
      return Action::LayoutChanged;
    case fui::QWERTY_KEY_BACKSPACE:
      return Action::Backspace;
    case fui::QWERTY_KEY_ENTER:
      return Action::Enter;
    case fui::QWERTY_KEY_SPACE:
      encodeUtf8(' ', out);
      return Action::Insert;
    default:
      if (value < 0x20) return Action::None;
      // The id is the character the key showed when pressed, even if the layer changed since.
      encodeUtf8(static_cast<uint16_t>(value), out);
      if (shifted && !capsLock && !symbols) {
        shifted = false;
        build();
      }
      return Action::Insert;
  }
}

bool TypingKeyboard::autoCapitalize(const std::string& text, const size_t cursor) {
  if (capsLock || symbols) return false;
  bool start = cursor == 0 || text[cursor - 1] == '\n';
  if (!start && cursor >= 2 && text[cursor - 1] == ' ') {
    const char p = text[cursor - 2];
    start = p == '.' || p == '!' || p == '?';
    if (!start) {
      // Right after a Markdown marker at the start of a line ("- ", "- [ ] ", "# ", "2. ").
      const size_t nl = text.rfind('\n', cursor - 1);
      const size_t from = nl == std::string::npos ? 0 : nl + 1;
      bool marker = false;
      start = true;
      for (size_t i = from; i < cursor && start; ++i) {
        const char c = text[i];
        marker = marker || c == '-' || c == '*' || c == '+' || c == '>' || c == '#' ||
                 (c == '.' && i > from && text[i - 1] >= '0' && text[i - 1] <= '9');
        start = c == ' ' || c == '[' || c == ']' || c == 'x' || c == '.' || (c >= '0' && c <= '9') || c == '-' ||
                c == '*' || c == '+' || c == '>' || c == '#';
      }
      start = start && marker;
    }
  }
  if (start == shifted) return false;
  shifted = start;
  build();
  return true;
}

void TypingKeyboard::draw(const GfxRenderer& renderer, const int16_t pressed) {
  const fui::Rect kb = rect(renderer);
  const int rowHeight = (kb.height - ROW_GAP * (ROWS - 1)) / ROWS;
  const bool upper = shifted || capsLock;

  hits.beginPublishCycle();
  hits.clear();  // the building generation still holds the frame before last
  for (uint8_t row = 0; row < ROWS; ++row) {
    const fui::KeyboardRow& kr = rows[row];
    int units = kr.insetUnits * 2;
    for (uint8_t c = 0; c < kr.count; ++c) units += kr.keys[c].widthUnits ? kr.keys[c].widthUnits : 1;
    const int unitW = (kb.width - GAP * (kr.count - 1)) / units;
    const int y = kb.y + row * (rowHeight + ROW_GAP);
    int x = kb.x + kr.insetUnits * unitW;
    for (uint8_t c = 0; c < kr.count; ++c) {
      const fui::KeyboardKey& key = kr.keys[c];
      const int w = unitW * (key.widthUnits ? key.widthUnits : 1);
      const bool down = key.value == pressed;
      if (down) renderer.fillRoundedRect(x, y, w, rowHeight, RADIUS, Color::Black);
      const bool ink = !down;
      const int cx = x + w / 2;
      const int cy = y + rowHeight / 2;
      switch (key.kind) {
        case fui::KeyKind::Shift:
          drawShiftGlyph(renderer, cx, cy, upper, capsLock, ink);
          break;
        case fui::KeyKind::Delete:
          drawDeleteGlyph(renderer, cx, cy, ink);
          break;
        case fui::KeyKind::Ok:
          drawReturnGlyph(renderer, cx, cy, ink);
          break;
        case fui::KeyKind::Lang:
          drawLabel(renderer, SMALL_FONT_ID, cx, cy, english ? "EN" : "RU", ink);
          break;
        case fui::KeyKind::Mode:
          drawLabel(renderer, SMALL_FONT_ID, cx, cy, symbols ? (english ? "ABC" : "АБВ") : "123", ink);
          break;
        case fui::KeyKind::Space:
          // A bar across the key, the same in every language.
          renderer.drawLine(cx - w * 2 / 5, cy + 3, cx + w * 2 / 5, cy + 3, 2, ink);
          break;
        default:
          if (key.value == KEY_SYMBOL_PAGE) {
            drawLabel(renderer, SMALL_FONT_ID, cx, cy, symbolsMore ? "123" : "#+=", ink);
            break;
          }
          drawLabel(renderer, UI_12_FONT_ID, cx, cy, key.label, ink);
          break;
      }
      fui::Interaction hit;
      hit.rect = fui::Rect{static_cast<int16_t>(x - GAP / 2), static_cast<int16_t>(y - ROW_GAP / 2),
                           static_cast<int16_t>(w + GAP), static_cast<int16_t>(rowHeight + ROW_GAP)};
      hit.action = ACTION_KEY;
      hit.value = key.value;
      hits.addInteraction(hit);
      x += w + GAP;
    }
  }
  hits.publish();
}

// The nearest key wins, so a touch in the gap between keys is never lost.
bool TypingKeyboard::keyAt(const int x, const int y, int16_t& value) const {
  const size_t count = hits.publishedCount();
  const fui::Interaction* items = hits.publishedData();
  constexpr int MAX_DISTANCE = 16;  // px outside any key still counts
  int best = MAX_DISTANCE * MAX_DISTANCE + 1;
  for (size_t i = 0; i < count; ++i) {
    const fui::Rect& r = items[i].rect;
    const int dx = x < r.x ? r.x - x : (x >= r.x + r.width ? x - (r.x + r.width - 1) : 0);
    const int dy = y < r.y ? r.y - y : (y >= r.y + r.height ? y - (r.y + r.height - 1) : 0);
    const int d = dx * dx + dy * dy;
    if (d < best) {
      best = d;
      value = items[i].value;
    }
  }
  return best <= MAX_DISTANCE * MAX_DISTANCE;
}

int16_t TypingKeyboard::alternateOf(const int16_t value) {
  switch (value) {
    case 0x0435:      // е
      return 0x0451;  // ё
    case 0x0415:      // Е
      return 0x0401;  // Ё
    default:
      return 0;
  }
}
