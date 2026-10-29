#ifdef CPX_TYPING_LAB
#include "TypingLabActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace freeink {
extern volatile uint8_t uc8279FastPllOverride;
extern volatile uint8_t uc8279FastLutFrames;
extern volatile uint32_t uc8279TimeStream, uc8279TimeSetup, uc8279TimeDrf, uc8279TimeSync;
}  // namespace freeink

namespace {
constexpr fui::ActionId ACTION_KEY = 1;
constexpr int MARGIN = 12;
#ifdef FREEINK_X4PRO_FAST_DU_SHORTCUT
constexpr const char* STRATEGY_NAMES[] = {"1 stock DU", "2 full DU", "3 window DU"};
#else
constexpr const char* STRATEGY_NAMES[] = {"1 stock", "2 full", "3 window"};
#endif

// UC8279 PLL (0x30) candidates for fast refreshes; 0 = driver default (0x0E).
constexpr uint8_t PLL_VALUES[] = {0x00, 0x0F, 0x0D, 0x0A, 0x1E, 0x2E, 0x3E, 0x3F};
constexpr uint8_t PLL_COUNT = sizeof(PLL_VALUES) / sizeof(PLL_VALUES[0]);
// Frames of the external single-phase DU LUT; 0 = factory OTP waveform.
constexpr uint8_t LUT_FRAMES[] = {0, 4, 6, 8, 12, 16};
constexpr uint8_t LUT_COUNT = sizeof(LUT_FRAMES) / sizeof(LUT_FRAMES[0]);

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
  layoutId = keyboard_layouts::startingLayout();
  freeink::uc8279FastLutFrames = LUT_FRAMES[lutIndex.load()];
  const uint16_t enabledLayouts = keyboard_layouts::enabled();
  showLangKey = (enabledLayouts & (enabledLayouts - 1)) != 0;
  fullRedraw = true;
  requestUpdate();
}

void TypingLabActivity::onExit() {
  freeink::uc8279FastPllOverride = 0;
  freeink::uc8279FastLutFrames = 0;
  Activity::onExit();
}

void TypingLabActivity::enqueue(const int16_t value, const uint32_t downMs) {
  const uint8_t head = queueHead.load(std::memory_order_relaxed);
  const uint8_t next = (head + 1) % QUEUE_SIZE;
  if (next == queueTail.load(std::memory_order_acquire)) return;  // full: drop
  queue[head] = PendingKey{value, downMs};
  queueHead.store(next, std::memory_order_release);
}

bool TypingLabActivity::keyAt(const int x, const int y, int16_t& value) const {
  const size_t count = interactions.publishedCount();
  const fui::Interaction* items = interactions.publishedData();
  for (size_t i = 0; i < count; ++i) {
    const fui::Rect& r = items[i].rect;
    if (items[i].action == ACTION_KEY && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
      value = items[i].value;
      return true;
    }
  }
  return false;
}

int TypingLabActivity::controlBarHeight() const { return renderer.getLineHeight(UI_12_FONT_ID) + 2 * MARGIN; }

// Three equal tap zones across the top bar: back, strategy, clear.
TypingLabActivity::Control TypingLabActivity::controlAt(const int x, const int y) const {
  // Second bar: left half cycles the LUT, right half the PLL.
  const int barHeight = controlBarHeight();
  if (y >= barHeight && y < 2 * barHeight) {
    return x < renderer.getScreenWidth() / 2 ? Control::Lut : Control::Pll;
  }
  if (y < 0 || y >= controlBarHeight()) return Control::None;
  const int third = renderer.getScreenWidth() / 3;
  if (x < third) return Control::Back;
  if (x < 2 * third) return Control::Mode;
  return Control::Clear;
}

void TypingLabActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const uint32_t shownAt = highlightShownAt.load();
  if (shownAt != 0 && millis() - shownAt > 1500 && queueTail.load() == queueHead.load() && !clearHighlight.load()) {
    clearHighlight = true;
    requestUpdate();
  }

  // Raw contact edge: act once when a finger lands, then wait for lift-off.
  int tx = 0;
  int ty = 0;
  const bool held = mappedInput.isScreenTouchHeld(tx, ty);
  const auto current = static_cast<Strategy>(strategy.load());
  if (!held) {
    if (contactActive && current == Strategy::Stock && stockDownValue != INT16_MIN) {
      enqueue(stockDownValue, stockDownMs);
      stockDownValue = INT16_MIN;
      highlightValue = INT16_MIN;
      requestUpdate();
    }
    contactActive = false;
    return;
  }
  if (contactActive) return;
  contactActive = true;
  const uint32_t now = millis();

  switch (controlAt(tx, ty)) {
    case Control::Back:
      onGoHome();
      return;
    case Control::Mode:
      strategy = (strategy.load() + 1) % static_cast<uint8_t>(Strategy::Count);
      fullRedraw = true;
      requestUpdate();
      return;
    case Control::Clear:
      enqueue(INT16_MIN, now);
      fullRedraw = true;
      requestUpdate();
      return;
    case Control::Lut: {
      const uint8_t next = (lutIndex.load() + 1) % LUT_COUNT;
      lutIndex = next;
      freeink::uc8279FastLutFrames = LUT_FRAMES[next];
      fullRedraw = true;
      requestUpdate();
      return;
    }
    case Control::Pll: {
      const uint8_t next = (pllIndex.load() + 1) % PLL_COUNT;
      pllIndex = next;
      freeink::uc8279FastPllOverride = PLL_VALUES[next];
      fullRedraw = true;
      requestUpdate();
      return;
    }
    case Control::None:
      break;
  }

  int16_t value = 0;
  if (!keyAt(tx, ty, value)) return;
  if (current == Strategy::Stock) {
    stockDownMs = now;
    stockDownValue = value;
    highlightValue = value;
  } else {
    enqueue(value, now);
  }
  requestUpdate();
}

const fui::KeyboardLayout& TypingLabActivity::currentLayout() const {
  return fui::builtinKeyboardLayout(layoutId, shifted, symbols, /*numberRow=*/!symbols, showLangKey && !symbols);
}

fui::Rect TypingLabActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int rows = currentLayout().rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int y = renderer.getScreenHeight() - MARGIN - height;
  return fui::Rect{static_cast<int16_t>((pageWidth - width) / 2), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

int TypingLabActivity::textBandBottom() const { return keyboardRect().y - MARGIN; }

bool TypingLabActivity::applyKey(const int16_t value) {
  switch (value) {
    case INT16_MIN:
      text.clear();
      return false;
    case fui::QWERTY_KEY_SHIFT:
      shifted = !shifted;
      return true;
    case fui::QWERTY_KEY_MODE:
      symbols = !symbols;
      shifted = false;
      return true;
    case fui::QWERTY_KEY_LANG:
      layoutId = keyboard_layouts::next(layoutId);
      shifted = false;
      return true;
    case fui::QWERTY_KEY_BACKSPACE: {
      if (text.empty()) return false;
      size_t i = text.size() - 1;
      while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
      text.erase(i);
      return false;
    }
    case fui::QWERTY_KEY_ENTER:
      text += '\n';
      return false;
    default: {
      const char* out = fui::keyboardOutputFor(currentLayout(), value);
      if (!out) return false;
      text += out;
      if (shifted && !symbols) {
        shifted = false;
        return true;
      }
      return false;
    }
  }
}

void TypingLabActivity::drawStats() const {
  const int pageWidth = renderer.getScreenWidth();
  const int third = pageWidth / 3;
  const int barHeight = controlBarHeight();
  const int labelY = MARGIN;
  char line[96];
  snprintf(line, sizeof(line), "Mode %s", STRATEGY_NAMES[strategy.load()]);
  renderer.drawText(UI_12_FONT_ID, MARGIN, labelY, "< Back");
  renderer.drawCenteredText(UI_12_FONT_ID, labelY, line);
  const int clearWidth = renderer.getTextWidth(UI_12_FONT_ID, "Clear");
  renderer.drawText(UI_12_FONT_ID, pageWidth - MARGIN - clearWidth, labelY, "Clear");
  renderer.drawLine(third, MARGIN / 2, third, barHeight - MARGIN / 2);
  renderer.drawLine(2 * third, MARGIN / 2, 2 * third, barHeight - MARGIN / 2);
  renderer.drawLine(0, barHeight - 1, pageWidth - 1, barHeight - 1);

  const uint8_t lutFrames = LUT_FRAMES[lutIndex.load()];
  const uint8_t pllValue = PLL_VALUES[pllIndex.load()];
  char label[32];
  if (lutFrames) {
    snprintf(label, sizeof(label), "LUT %uf", lutFrames);
  } else {
    snprintf(label, sizeof(label), "LUT otp");
  }
  renderer.drawText(UI_12_FONT_ID, MARGIN, barHeight + MARGIN, label);
  snprintf(label, sizeof(label), "PLL %02X", pllValue ? pllValue : 0x0E);
  const int pllWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  renderer.drawText(UI_12_FONT_ID, pageWidth - MARGIN - pllWidth, barHeight + MARGIN, label);
  renderer.drawLine(pageWidth / 2, barHeight + MARGIN / 2, pageWidth / 2, 2 * barHeight - MARGIN / 2);
  renderer.drawLine(0, 2 * barHeight - 1, pageWidth - 1, 2 * barHeight - 1);

  const int smallHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const uint32_t avg = refreshCount ? latencySumMs / refreshCount : 0;
  const uint8_t pll = PLL_VALUES[pllIndex.load()];
  const uint8_t lut = LUT_FRAMES[lutIndex.load()];
  char lutName[8];
  if (lut) {
    snprintf(lutName, sizeof(lutName), "%uf", lut);
  } else {
    snprintf(lutName, sizeof(lutName), "otp");
  }
  snprintf(line, sizeof(line), "key %lums  avg %lums  n %lu  lut %s", static_cast<unsigned long>(lastLatencyMs),
           static_cast<unsigned long>(avg), static_cast<unsigned long>(refreshCount), lutName);
  renderer.drawText(SMALL_FONT_ID, MARGIN, 2 * barHeight + MARGIN / 2, line);
  snprintf(line, sizeof(line), "draw %lums  panel %lums  batch %u  keys %lu  pll %02X",
           static_cast<unsigned long>(lastDrawMs), static_cast<unsigned long>(lastPanelMs), lastBatch,
           static_cast<unsigned long>(keyCount), pll ? pll : 0x0E);
  renderer.drawText(SMALL_FONT_ID, MARGIN, 2 * barHeight + MARGIN / 2 + smallHeight, line);
  snprintf(line, sizeof(line), "spi %lu  setup %lu  drf %lu  sync %lu  cleanups %lu",
           static_cast<unsigned long>(freeink::uc8279TimeStream), static_cast<unsigned long>(freeink::uc8279TimeSetup),
           static_cast<unsigned long>(freeink::uc8279TimeDrf), static_cast<unsigned long>(freeink::uc8279TimeSync),
           static_cast<unsigned long>(cleanupCount));
  renderer.drawText(SMALL_FONT_ID, MARGIN, 2 * barHeight + MARGIN / 2 + 2 * smallHeight, line);
}

void TypingLabActivity::drawText() const {
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int top = 2 * controlBarHeight() + MARGIN / 2 + 3 * renderer.getLineHeight(SMALL_FONT_ID) + MARGIN;
  const int bottom = textBandBottom();
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
  renderer.fillRect(cursorX + 1, y - lineHeight, 2, lineHeight);
}

void TypingLabActivity::drawKeyboard(const int16_t highlight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::KeyboardLayout& layout = currentLayout();
  int16_t selected = -1;
  if (highlight != INT16_MIN) {
    int16_t index = 0;
    for (uint8_t r = 0; r < layout.rowCount && selected < 0; ++r) {
      for (uint8_t c = 0; c < layout.rows[r].count; ++c, ++index) {
        if (layout.rows[r].keys[c].value == highlight) {
          selected = index;
          break;
        }
      }
    }
  }

  interactions.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<56> frame(target, device, noInput, interactions);

  fui::KeyboardProps props;
  props.layout = &layout;
  props.keyAction = ACTION_KEY;
  props.okLabel = "\xE2\x86\xB5";  // return arrow: Enter inserts a newline here
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.selectedIndex = selected;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  fui::keyboard(frame, keyboardRect(), props);
  interactions.publish();
}

void TypingLabActivity::render(RenderLock&&) {
  const uint32_t renderStart = millis();

  // Drain every key that arrived since the last frame into one refresh.
  uint32_t oldestDownMs = 0;
  uint8_t batch = 0;
  bool keyboardChanged = false;
  uint8_t tail = queueTail.load(std::memory_order_relaxed);
  while (tail != queueHead.load(std::memory_order_acquire)) {
    const PendingKey key = queue[tail];
    if (batch == 0) oldestDownMs = key.downMs;
    keyboardChanged |= applyKey(key.value);
    typedHighlight = key.value == INT16_MIN ? INT16_MIN : key.value;
    ++batch;
    tail = (tail + 1) % QUEUE_SIZE;
  }
  queueTail.store(tail, std::memory_order_release);

  const auto current = static_cast<Strategy>(strategy.load());
  // Idle cleanup: drop the highlight and repaint once with the factory waveform to clear short-LUT residue.
  const bool cleanup = batch == 0 && clearHighlight.exchange(false);
  if (cleanup) typedHighlight = INT16_MIN;
  // Stock shows the key while held; the others show the last typed key in the same frame as its character.
  const int16_t highlight = current == Strategy::Stock ? highlightValue.load() : typedHighlight;
  const bool whole = fullRedraw.exchange(false);
  const int pageWidth = renderer.getScreenWidth();
  const int band = textBandBottom();

  if (whole || keyboardChanged || current != Strategy::Window) {
    renderer.clearScreen();
    drawStats();
    drawText();
    drawKeyboard(current == Strategy::Stock ? highlight : INT16_MIN);
    if (current != Strategy::Stock && highlight != INT16_MIN) {
      // Outline the typed key: a thin frame clears cleanly with a short waveform, a solid fill does not.
      const size_t count = interactions.publishedCount();
      const fui::Interaction* items = interactions.publishedData();
      for (size_t i = 0; i < count; ++i) {
        if (items[i].action == ACTION_KEY && items[i].value == highlight) {
          const fui::Rect& r = items[i].rect;
          renderer.drawRect(r.x + 2, r.y + 2, r.width - 4, r.height - 4, 3, true);
          break;
        }
      }
    }
  } else {
    renderer.fillRect(0, 0, pageWidth, band, false);
    drawStats();
    drawText();
  }

  const uint32_t drawDone = millis();
  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    const uint8_t frames = freeink::uc8279FastLutFrames;
    freeink::uc8279FastLutFrames = 0;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    freeink::uc8279FastLutFrames = frames;
    ++cleanupCount;
  } else if (current == Strategy::Window && !keyboardChanged) {
    renderer.displayWindow(0, 0, pageWidth, band);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t panelDone = millis();
  highlightShownAt = (current != Strategy::Window && highlight != INT16_MIN) ? panelDone : 0;

  if (batch > 0 && !whole) {
    lastLatencyMs = panelDone - oldestDownMs;
    lastDrawMs = drawDone - renderStart;
    lastPanelMs = panelDone - drawDone;
    lastBatch = batch;
    latencySumMs += lastLatencyMs;
    ++refreshCount;
    keyCount += batch;
  }
  if (whole) {
    latencySumMs = 0;
    refreshCount = 0;
    keyCount = 0;
    lastLatencyMs = lastDrawMs = lastPanelMs = 0;
    lastBatch = 0;
  }
}

#endif  // CPX_TYPING_LAB
