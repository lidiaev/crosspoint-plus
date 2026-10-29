#ifdef CPX_TYPING_LAB
#include "TypingLabActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "MappedInputManager.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace freeink {
extern volatile uint8_t uc8279FastLutFrames;
extern volatile uint8_t uc8279FastLutWhiteFrames;
extern volatile uint32_t uc8279TimeStream, uc8279TimeSetup, uc8279TimeDrf, uc8279TimeSync;
extern volatile bool uc8279WindowedFast;
extern volatile uint8_t uc8279LastWindowCount;
extern volatile bool uc8279ScanWindow;
}  // namespace freeink

namespace {
constexpr fui::ActionId ACTION_KEY = 1;
constexpr int MARGIN = 12;
constexpr uint32_t CLEANUP_IDLE_MS = 1200;
constexpr uint32_t RESTORE_IDLE_MS = 300;  // pressed key returns to normal after this pause

// Short-LUT frames {to black, to white}; {0, 0} = factory OTP waveform.
struct LutPair {
  uint8_t black;
  uint8_t white;
};
constexpr LutPair LUTS[] = {{0, 0}, {8, 12}, {8, 16}, {8, 8}, {6, 12}, {12, 12}, {12, 16}};
constexpr uint8_t LUT_COUNT = sizeof(LUTS) / sizeof(LUTS[0]);

size_t utf8Next(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && (static_cast<uint8_t>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}
}  // namespace

void TypingLabActivity::onEnter() {
  Activity::onEnter();
  text.reserve(4096);
  layoutId = keyboard_layouts::startingLayout() == fui::KeyboardLayoutId::QwertyEn ? fui::KeyboardLayoutId::QwertyEn
                                                                                   : fui::KeyboardLayoutId::CyrillicRu;
  buildLetterLayout();
  autoCapitalize();
  const uint16_t enabledLayouts = keyboard_layouts::enabled();
  showLangKey = (enabledLayouts & (enabledLayouts - 1)) != 0;
  applyLut();
  freeink::uc8279WindowedFast = windowed.load();
  fullRedraw = true;
  requestUpdate();
}

void TypingLabActivity::onExit() {
  freeink::uc8279FastLutFrames = 0;
  freeink::uc8279FastLutWhiteFrames = 0;
  freeink::uc8279WindowedFast = false;
  freeink::uc8279ScanWindow = false;
  Activity::onExit();
}

void TypingLabActivity::applyLut() const {
  const LutPair& lut = LUTS[lutIndex.load()];
  freeink::uc8279FastLutFrames = lut.black;
  freeink::uc8279FastLutWhiteFrames = lut.white;
}

void TypingLabActivity::enqueue(const int16_t value, const uint32_t downMs) {
  const uint8_t head = queueHead.load(std::memory_order_relaxed);
  const uint8_t next = (head + 1) % QUEUE_SIZE;
  if (next == queueTail.load(std::memory_order_acquire)) return;  // full: drop
  queue[head] = PendingKey{value, downMs};
  queueHead.store(next, std::memory_order_release);
}

// The nearest key wins, so a touch in the gap between keys is never lost.
bool TypingLabActivity::keyAt(const int x, const int y, int16_t& value) const {
  const size_t count = interactions.publishedCount();
  const fui::Interaction* items = interactions.publishedData();
  constexpr int MAX_DISTANCE = 16;  // px outside any key still counts
  int best = MAX_DISTANCE * MAX_DISTANCE + 1;
  for (size_t i = 0; i < count; ++i) {
    if (items[i].action != ACTION_KEY) continue;
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

int TypingLabActivity::barHeight() const { return renderer.getLineHeight(UI_12_FONT_ID) + 2 * MARGIN; }

// Three equal tap zones across the top bar.
TypingLabActivity::Control TypingLabActivity::controlAt(const int x, const int y) const {
  if (y < 0 || y >= barHeight()) return Control::None;
  const int quarter = renderer.getScreenWidth() / 4;
  if (x < quarter) return Control::Back;
  if (x < 2 * quarter) return Control::Hit;
  if (x < 3 * quarter) return Control::Scan;
  return Control::Clear;
}

void TypingLabActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const uint32_t now = millis();
  const bool queueEmpty = queueTail.load() == queueHead.load();

  // Idle work: one factory-waveform repaint clears short-LUT residue.
  const uint32_t keyFrameAt = lastKeyFrameAt.load();
  if (keyFrameAt != 0 && now - keyFrameAt > CLEANUP_IDLE_MS && queueEmpty && !cleanupFrame.load()) {
    cleanupFrame = true;
    requestUpdate();
  }

  // The inverted key returns to normal with the next key, or after a short pause.
  const uint32_t shownAt = pressedShownAt.load();
  if (shownAt != 0 && !contactActive && queueEmpty && now - std::max(shownAt, liftedAt) > RESTORE_IDLE_MS &&
      !releaseFrame.load()) {
    releaseFrame = true;
    requestUpdate();
  }

  pollTouch(now);
}

void TypingLabActivity::pollTouch(const uint32_t now) {
  constexpr int MATCH_PX = 48;  // same finger if it moved less than this between samples
  constexpr int EDGE_PX = 8;    // a contact landing on the side edge is a gripping thumb
  constexpr uint32_t HIT_DELAY_MS = 30;

  HalGPIO::TouchContact contacts[MAX_TRACKS];
  const uint8_t count = gpio.getTouchContacts(contacts, MAX_TRACKS);
  for (Track& t : tracks) t.seen = false;

  const int width = renderer.getScreenWidth();
  for (uint8_t i = 0; i < count; ++i) {
    int x = 0;
    int y = 0;
    renderer.tapToLogical(contacts[i].nx, contacts[i].ny, x, y);
    Track* match = nullptr;
    int best = MATCH_PX * MATCH_PX;
    for (Track& t : tracks) {
      if (!t.active || t.seen) continue;
      const int d = (t.x - x) * (t.x - x) + (t.y - y) * (t.y - y);
      if (d < best) {
        best = d;
        match = &t;
      }
    }
    if (!match) {
      for (Track& t : tracks) {
        if (t.active) continue;
        t = Track{true, false, false, 0, 0, now, INT16_MIN, 0};
        t.committed = x < EDGE_PX || x >= width - EDGE_PX;
        match = &t;
        break;
      }
    }
    if (!match) continue;
    match->x = static_cast<int16_t>(x);
    match->y = static_cast<int16_t>(y);
    match->seen = true;
  }

  bool anyActive = false;
  for (Track& t : tracks) {
    if (!t.active) continue;
    if (!t.seen) {
      // Lifted: a tap shorter than the hit delay still counts, at its last position.
      if (!t.committed) commitPress(t.x, t.y, t.since);
      t.active = false;
      continue;
    }
    anyActive = true;
    if (!t.committed && (!hitDelay.load() || now - t.since >= HIT_DELAY_MS)) {
      t.committed = true;
      t.value = commitPress(t.x, t.y, t.since);
      t.nextRepeatAt = t.since + 450;
    }
    // Holding Backspace repeats, faster the longer it is held.
    if (t.committed && t.value == fui::QWERTY_KEY_BACKSPACE && now >= t.nextRepeatAt) {
      enqueue(fui::QWERTY_KEY_BACKSPACE, now);
      t.nextRepeatAt = now + (now - t.since > 1500 ? 50 : 110);
      requestUpdate();
    }
  }
  if (contactActive && !anyActive) liftedAt = now;
  contactActive = anyActive;
}

int16_t TypingLabActivity::commitPress(const int x, const int y, const uint32_t downMs) {
  switch (controlAt(x, y)) {
    case Control::Back:
      onGoHome();
      return INT16_MIN;
    case Control::Hit:
      hitDelay = !hitDelay.load();
      fullRedraw = true;
      requestUpdate();
      return INT16_MIN;
    case Control::Scan:
      scanWindow = !scanWindow.load();
      freeink::uc8279ScanWindow = scanWindow.load();
      fullRedraw = true;
      requestUpdate();
      return INT16_MIN;
    case Control::Clear:
      enqueue(CLEAR_TEXT, downMs);
      fullRedraw = true;
      requestUpdate();
      return INT16_MIN;
    case Control::None:
      break;
  }
  int16_t value = 0;
  if (!keyAt(x, y, value)) return INT16_MIN;
  dotX = static_cast<int16_t>(x);
  dotY = static_cast<int16_t>(y);
  enqueue(value, downMs);
  requestUpdate();
  return value;
}

namespace {
// Key rows: UTF-8 characters separated by spaces. Key ids are the code points.
constexpr const char* RU_LOWER[3] = {"й ц у к е н г ш щ з х ъ", "ф ы в а п р о л д ж э", "я ч с м и т ь б ю"};
constexpr const char* RU_UPPER[3] = {"Й Ц У К Е Н Г Ш Щ З Х Ъ", "Ф Ы В А П Р О Л Д Ж Э", "Я Ч С М И Т Ь Б Ю"};
constexpr const char* EN_LOWER[3] = {"q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m"};
constexpr const char* EN_UPPER[3] = {"Q W E R T Y U I O P", "A S D F G H J K L", "Z X C V B N M"};
constexpr const char* SYMBOLS[3] = {"1 2 3 4 5 6 7 8 9 0", "- / : ; ( ) « » @ \"", "№ % + = ? ! '"};

// Decodes one UTF-8 code point starting at s; returns its length.
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
}  // namespace

// iPhone-like layers: Shift and Backspace flank the third row; the bottom row has
// the same widths in every layer, so switching language or symbols never moves it.
void TypingLabActivity::buildLetterLayout() {
  const bool ru = layoutId != fui::KeyboardLayoutId::QwertyEn;
  const bool upper = shifted || capsLock;
  const char* const* rows = symbols ? SYMBOLS : ru ? (upper ? RU_UPPER : RU_LOWER) : (upper ? EN_UPPER : EN_LOWER);
  static char glyphs[KB_ROWS][KB_COLS][4];  // NUL-terminated characters referenced as label/output
  for (uint8_t r = 0; r < 3; ++r) {
    uint8_t n = 0;
    if (r == 2) {
      kbKeys[r][n++] =
          symbols ? fui::KeyboardKey{"#", "#", fui::KeyKind::Normal, fui::StateNormal, '#', 3, true}
                  : fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Shift, fui::StateNormal, fui::QWERTY_KEY_SHIFT,
                                     3,       true};
    }
    for (const char* p = rows[r]; *p && n < KB_COLS - 1;) {
      if (*p == ' ') {
        ++p;
        continue;
      }
      int16_t cp = 0;
      const int len = decodeUtf8(p, cp);
      char* g = glyphs[r][n];
      memcpy(g, p, len);
      g[len] = '\0';
      kbKeys[r][n++] = fui::KeyboardKey{g, g, fui::KeyKind::Normal, fui::StateNormal, cp, 2, true};
      p += len;
    }
    if (r == 2) {
      kbKeys[r][n++] = fui::KeyboardKey{
          nullptr, nullptr, fui::KeyKind::Delete, fui::StateNormal, fui::QWERTY_KEY_BACKSPACE, 3, true};
    }
    kbRows[r] = fui::KeyboardRow{kbKeys[r], n, static_cast<uint8_t>(r == 1 && !symbols ? 1 : 0)};
  }
  uint8_t n = 0;
  kbKeys[3][n++] =
      fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Mode, fui::StateNormal, fui::QWERTY_KEY_MODE, 3, true};
  kbKeys[3][n++] =
      fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Lang, fui::StateNormal, fui::QWERTY_KEY_LANG, 3, true};
  kbKeys[3][n++] = fui::KeyboardKey{",", ",", fui::KeyKind::Normal, fui::StateNormal, ',', 2, true};
  kbKeys[3][n++] =
      fui::KeyboardKey{nullptr, " ", fui::KeyKind::Space, fui::StateNormal, fui::QWERTY_KEY_SPACE, 9, true};
  kbKeys[3][n++] = fui::KeyboardKey{".", ".", fui::KeyKind::Normal, fui::StateNormal, '.', 2, true};
  kbKeys[3][n++] =
      fui::KeyboardKey{nullptr, nullptr, fui::KeyKind::Ok, fui::StateNormal, fui::QWERTY_KEY_ENTER, 5, true};
  kbRows[3] = fui::KeyboardRow{kbKeys[3], n, 0};
  kbLayout = fui::KeyboardLayout{kbRows, KB_ROWS};
}

const fui::KeyboardLayout& TypingLabActivity::currentLayout() const { return kbLayout; }

fui::Rect TypingLabActivity::keyboardRect() const {
  constexpr int KEY_HEIGHT = 60;
  constexpr int ROW_GAP = 8;
  constexpr int SIDE = 4;
  const int rows = currentLayout().rowCount;
  const int height = rows * KEY_HEIGHT + (rows - 1) * ROW_GAP;
  const int y = renderer.getScreenHeight() - SIDE - height;
  return fui::Rect{SIDE, static_cast<int16_t>(y), static_cast<int16_t>(renderer.getScreenWidth() - 2 * SIDE),
                   static_cast<int16_t>(height)};
}

// Capital letter at the start of the text and after a sentence end.
void TypingLabActivity::autoCapitalize() {
  if (capsLock || symbols) return;
  const size_t n = text.size();
  bool start = n == 0 || text[n - 1] == '\n';
  if (!start && n >= 2 && text[n - 1] == ' ') {
    const char p = text[n - 2];
    start = p == '.' || p == '!' || p == '?';
  }
  if (start != shifted) {
    shifted = start;
    buildLetterLayout();
  }
}

bool TypingLabActivity::applyKey(const int16_t value, const uint32_t downMs) {
  bool changed = false;
  switch (value) {
    case CLEAR_TEXT:
      text.clear();
      break;
    case fui::QWERTY_KEY_SHIFT:
      if (capsLock) {
        capsLock = false;
        shifted = false;
      } else if (shifted && downMs - lastShiftAt < 400) {
        capsLock = true;  // double tap
      } else {
        shifted = !shifted;
      }
      lastShiftAt = downMs;
      buildLetterLayout();
      return true;
    case fui::QWERTY_KEY_MODE:
      symbols = !symbols;
      shifted = false;
      buildLetterLayout();
      return true;
    case fui::QWERTY_KEY_LANG:
      layoutId = layoutId == fui::KeyboardLayoutId::QwertyEn ? fui::KeyboardLayoutId::CyrillicRu
                                                             : fui::KeyboardLayoutId::QwertyEn;
      changed = true;
      break;
    case fui::QWERTY_KEY_BACKSPACE: {
      if (text.empty()) break;
      size_t i = text.size() - 1;
      while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
      text.erase(i);
      break;
    }
    case fui::QWERTY_KEY_ENTER:
      text += '\n';
      break;
    case fui::QWERTY_KEY_SPACE: {
      // Double space after a word ends the sentence.
      const size_t n = text.size();
      if (n >= 2 && text[n - 1] == ' ' && text[n - 2] != ' ' && text[n - 2] != '.' && text[n - 2] != '\n') {
        text[n - 1] = '.';
      }
      text += ' ';
      break;
    }
    default: {
      // Letter ids are their code points: insert the letter the key showed when it was
      // pressed, even if Shift or auto-capitalization has switched the layer since.
      const char* out = fui::keyboardOutputFor(currentLayout(), value);
      char encoded[4] = {};
      if (!out && value >= 0x20 && value < 0x80) {
        encoded[0] = static_cast<char>(value);
        out = encoded;
      } else if (!out && value >= 0x80 && value < 0x800) {
        encoded[0] = static_cast<char>(0xC0 | (value >> 6));
        encoded[1] = static_cast<char>(0x80 | (value & 0x3F));
        out = encoded;
      } else if (!out && static_cast<uint16_t>(value) >= 0x800) {
        const uint16_t v = static_cast<uint16_t>(value);
        encoded[0] = static_cast<char>(0xE0 | (v >> 12));
        encoded[1] = static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        encoded[2] = static_cast<char>(0x80 | (v & 0x3F));
        out = encoded;
      }
      if (!out) return false;
      text += out;
      if (shifted && !capsLock && !symbols) {
        shifted = false;
        changed = true;
      }
      break;
    }
  }
  const bool wasShifted = shifted;
  if (changed) buildLetterLayout();
  autoCapitalize();
  return changed || wasShifted != shifted;
}

void TypingLabActivity::drawBars() const {
  const int pageWidth = renderer.getScreenWidth();
  const int quarter = pageWidth / 4;
  const int bar = barHeight();
  const LutPair& lut = LUTS[lutIndex.load()];
  char lutLabel[16];
  if (lut.black) {
    snprintf(lutLabel, sizeof(lutLabel), "LUT %u/%u", lut.black, lut.white);
  } else {
    snprintf(lutLabel, sizeof(lutLabel), "LUT otp");
  }
  (void)lutLabel;
  const char* labels[4] = {"< Back", hitDelay.load() ? "Hit 30ms" : "Hit 0", scanWindow.load() ? "Scan on" : "Scan off",
                           "Clear"};
  for (int i = 0; i < 4; ++i) {
    const int width = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    renderer.drawText(SMALL_FONT_ID, i * quarter + (quarter - width) / 2, MARGIN, labels[i]);
    if (i > 0) renderer.drawLine(i * quarter, MARGIN / 2, i * quarter, bar - MARGIN / 2);
  }
  renderer.drawLine(0, bar - 1, pageWidth - 1, bar - 1);

  char line[112];
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const uint32_t avg = refreshCount ? latencySumMs / refreshCount : 0;
  snprintf(line, sizeof(line), "key %lums  avg %lums  keys %lu  frames %lu  panel %lums  cl %lu",
           static_cast<unsigned long>(lastLatencyMs), static_cast<unsigned long>(avg),
           static_cast<unsigned long>(keyCount), static_cast<unsigned long>(refreshCount),
           static_cast<unsigned long>(lastPanelMs), static_cast<unsigned long>(cleanupCount));
  renderer.drawText(SMALL_FONT_ID, MARGIN, bar + MARGIN / 2, line);
  snprintf(line, sizeof(line), "spi %lu  setup %lu  drf %lu  sync %lu  win %u", static_cast<unsigned long>(keyStream),
           static_cast<unsigned long>(keySetup), static_cast<unsigned long>(keyDrf),
           static_cast<unsigned long>(keySync), keyWindows);
  renderer.drawText(SMALL_FONT_ID, MARGIN, bar + MARGIN / 2 + small, line);
}

void TypingLabActivity::drawText(const bool cursor) const {
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int top = barHeight() + MARGIN / 2 + 2 * renderer.getLineHeight(SMALL_FONT_ID) + MARGIN;
  const int bottom = keyboardRect().y - MARGIN;
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN;
  const int maxLines = (bottom - top) / lineHeight;
  if (maxLines <= 0) return;

  // Greedy wrap by code point; only the last maxLines lines are shown.
  constexpr int MAX_LINES = 32;
  size_t starts[MAX_LINES];
  size_t ends[MAX_LINES];
  int lines = 0;
  size_t lineStart = 0;
  int width = 0;
  char glyph[8];
  for (size_t i = 0; i <= text.size();) {
    if (i == text.size() || text[i] == '\n') {
      starts[lines % MAX_LINES] = lineStart;
      ends[lines % MAX_LINES] = i;
      ++lines;
      if (i == text.size()) break;
      lineStart = ++i;
      width = 0;
      continue;
    }
    const size_t next = utf8Next(text, i);
    const size_t n = std::min(next - i, sizeof(glyph) - 1);
    memcpy(glyph, text.data() + i, n);
    glyph[n] = '\0';
    const int advance = renderer.getTextAdvanceX(UI_12_FONT_ID, glyph, EpdFontFamily::REGULAR);
    if (width + advance > maxWidth && i > lineStart) {
      starts[lines % MAX_LINES] = lineStart;
      ends[lines % MAX_LINES] = i;
      ++lines;
      lineStart = i;
      width = 0;
    }
    width += advance;
    i = next;
  }

  const int shown = std::min(std::min(lines, maxLines), MAX_LINES);
  std::string lineText;
  int y = top;
  int cursorX = MARGIN;
  for (int l = lines - shown; l < lines; ++l) {
    lineText.assign(text, starts[l % MAX_LINES], ends[l % MAX_LINES] - starts[l % MAX_LINES]);
    renderer.drawText(UI_12_FONT_ID, MARGIN, y, lineText.c_str());
    cursorX = MARGIN + renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    y += lineHeight;
  }
  if (cursor) renderer.fillRect(cursorX + 1, y - lineHeight, 2, lineHeight);
}

namespace {
// System-key glyphs: one 2 px outline style, ~22 px tall, centered on (cx, cy).
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

void drawCenteredLabel(const GfxRenderer& r, int font, int cx, int cy, const char* label, bool ink) {
  if (!label || !*label) return;
  const int tw = r.getTextWidth(font, label);
  const int th = r.getLineHeight(font);
  r.drawText(font, cx - tw / 2, cy - th / 2 - 2, label, ink);
}
}  // namespace

// Clean layout: no key borders or fills; a pressed key is the only filled shape.
// Hit rects cover the gaps between keys and are published for loop().
void TypingLabActivity::drawKeyboard(const int16_t pressed) {
  constexpr int GAP = 6;
  constexpr int ROW_GAP = 8;
  constexpr int RADIUS = 7;
  const fui::KeyboardLayout& layout = currentLayout();
  const fui::Rect kb = keyboardRect();
  const int rowHeight = (kb.height - ROW_GAP * (layout.rowCount - 1)) / layout.rowCount;
  const bool upper = shifted || capsLock;
  const bool en = layoutId == fui::KeyboardLayoutId::QwertyEn;

  interactions.beginPublishCycle();
  for (uint8_t row = 0; row < layout.rowCount; ++row) {
    const fui::KeyboardRow& kr = layout.rows[row];
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
          drawCenteredLabel(renderer, SMALL_FONT_ID, cx, cy, en ? "EN" : "RU", ink);
          break;
        case fui::KeyKind::Mode:
          drawCenteredLabel(renderer, SMALL_FONT_ID, cx, cy, symbols ? (en ? "ABC" : "АБВ") : "123", ink);
          break;
        case fui::KeyKind::Space:
          drawCenteredLabel(renderer, SMALL_FONT_ID, cx, cy, en ? "space" : "пробел", ink);
          break;
        default:
          drawCenteredLabel(renderer, UI_12_FONT_ID, cx, cy, key.label, ink);
          break;
      }
      fui::Interaction hit;
      hit.rect = fui::Rect{static_cast<int16_t>(x - GAP / 2), static_cast<int16_t>(y - ROW_GAP / 2),
                           static_cast<int16_t>(w + GAP), static_cast<int16_t>(rowHeight + ROW_GAP)};
      hit.action = ACTION_KEY;
      hit.value = key.value;
      interactions.addInteraction(hit);
      x += w + GAP;
    }
  }
  interactions.publish();
}

void TypingLabActivity::render(RenderLock&&) {
  // Drain every key that arrived since the last frame into one refresh.
  uint32_t oldestDownMs = 0;
  uint8_t batch = 0;
  bool keyboardChanged = false;
  uint8_t tail = queueTail.load(std::memory_order_relaxed);
  while (tail != queueHead.load(std::memory_order_acquire)) {
    const PendingKey key = queue[tail];
    if (batch == 0) oldestDownMs = key.downMs;
    keyboardChanged |= applyKey(key.value, key.downMs);
    pressedKey = key.value == CLEAR_TEXT ? INT16_MIN : key.value;
    ++batch;
    tail = (tail + 1) % QUEUE_SIZE;
  }
  queueTail.store(tail, std::memory_order_release);

  const bool whole = fullRedraw.exchange(false);
  // A lift-off that races ahead of its key frame stays pending for the next frame.
  const bool release = batch == 0 && releaseFrame.exchange(false) && pressedKey != INT16_MIN;
  const bool cleanup = cleanupFrame.exchange(false) && batch == 0;
  if (release || cleanup) pressedKey = INT16_MIN;
  if (!whole && batch == 0 && !release && !cleanup) return;

  renderer.clearScreen();
  drawBars();
  drawText(true);
  drawKeyboard(pressedKey);
  const int16_t dx = dotX.load();
  const int16_t dy = dotY.load();
  if (showDot.load() && dx >= 0) {
    // Where the last press registered: a small cross over the keyboard.
    renderer.drawLine(dx - 7, dy, dx + 7, dy, 4, false);  // white halo: visible on an inverted key
    renderer.drawLine(dx, dy - 7, dx, dy + 7, 4, false);
    renderer.drawLine(dx - 6, dy, dx + 6, dy, 2, true);
    renderer.drawLine(dx, dy - 6, dx, dy + 6, 2, true);
  }

  const uint32_t drawDone = millis();
  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    // Full transfer on cleanup: resyncs the controller planes even if a window was ever missed.
    freeink::uc8279FastLutFrames = 0;
    freeink::uc8279WindowedFast = false;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    freeink::uc8279WindowedFast = windowed.load();
    applyLut();
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t panelDone = millis();

  if (batch > 0 || release) lastKeyFrameAt = panelDone;
  pressedShownAt = pressedKey != INT16_MIN ? panelDone : 0;
  if (batch > 0) {
    keyStream = freeink::uc8279TimeStream;
    keySetup = freeink::uc8279TimeSetup;
    keyDrf = freeink::uc8279TimeDrf;
    keySync = freeink::uc8279TimeSync;
    keyWindows = freeink::uc8279WindowedFast ? freeink::uc8279LastWindowCount : 0;
    lastLatencyMs = panelDone - oldestDownMs;
    lastPanelMs = panelDone - drawDone;
    latencySumMs += lastLatencyMs;
    ++refreshCount;
    keyCount += batch;
  }
  if (cleanup || whole) {
    lastKeyFrameAt = 0;
    if (cleanup) ++cleanupCount;
  }
  if (releaseFrame.load()) requestUpdate();
  if (whole) {
    latencySumMs = 0;
    refreshCount = 0;
    keyCount = 0;
    lastLatencyMs = lastPanelMs = 0;
  }
}

#endif  // CPX_TYPING_LAB
