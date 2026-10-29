#ifdef CPX_TYPING_LAB
#include "TypingLabActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace freeink {
extern volatile uint8_t uc8279FastLutFrames;
extern volatile uint8_t uc8279FastLutWhiteFrames;
extern volatile uint32_t uc8279TimeStream, uc8279TimeSetup, uc8279TimeDrf, uc8279TimeSync;
}  // namespace freeink

namespace {
constexpr fui::ActionId ACTION_KEY = 1;
constexpr int MARGIN = 12;
constexpr uint32_t CLEANUP_IDLE_MS = 1200;
constexpr uint32_t BLINK_PERIOD_MS = 600;
constexpr uint8_t BLINK_TOGGLES = 8;  // even: the cursor ends visible

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
  layoutId = keyboard_layouts::startingLayout();
  const uint16_t enabledLayouts = keyboard_layouts::enabled();
  showLangKey = (enabledLayouts & (enabledLayouts - 1)) != 0;
  applyLut();
  fullRedraw = true;
  requestUpdate();
}

void TypingLabActivity::onExit() {
  freeink::uc8279FastLutFrames = 0;
  freeink::uc8279FastLutWhiteFrames = 0;
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

int TypingLabActivity::barHeight() const { return renderer.getLineHeight(UI_12_FONT_ID) + 2 * MARGIN; }

// Four equal tap zones across the top bar.
TypingLabActivity::Control TypingLabActivity::controlAt(const int x, const int y) const {
  if (y < 0 || y >= barHeight()) return Control::None;
  const int quarter = renderer.getScreenWidth() / 4;
  if (x < quarter) return Control::Back;
  if (x < 2 * quarter) return Control::Lut;
  if (x < 3 * quarter) return Control::Blink;
  return Control::Clear;
}

void TypingLabActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const uint32_t now = millis();
  const bool queueEmpty = queueTail.load() == queueHead.load();

  // Idle work: one factory-waveform repaint, then an optional short cursor blink.
  const uint32_t keyFrameAt = lastKeyFrameAt.load();
  if (keyFrameAt != 0 && now - keyFrameAt > CLEANUP_IDLE_MS && queueEmpty && !cleanupFrame.load()) {
    cleanupFrame = true;
    requestUpdate();
  }
  const uint32_t cleanedAt = cleanupDoneAt.load();
  if (blinkEnabled.load() && cleanedAt != 0 && cleanedAt != blinkStartedFor) {
    blinkStartedFor = cleanedAt;
    blinkUntil = BLINK_TOGGLES;
    nextBlinkAt = cleanedAt + BLINK_PERIOD_MS;
  }
  if (blinkUntil > 0 && keyFrameAt == 0 && queueEmpty && now >= nextBlinkAt && !blinkFrame.load()) {
    blinkFrame = true;
    --blinkUntil;
    nextBlinkAt = now + BLINK_PERIOD_MS;
    requestUpdate();
  }

  // Raw contact edge: act once when a finger lands, then wait for lift-off.
  int tx = 0;
  int ty = 0;
  const bool held = mappedInput.isScreenTouchHeld(tx, ty);
  if (!held) {
    if (contactActive) {
      releaseFrame = true;  // drop the key outline as soon as the finger lifts
      requestUpdate();
    }
    contactActive = false;
    return;
  }
  if (contactActive) return;
  contactActive = true;

  switch (controlAt(tx, ty)) {
    case Control::Back:
      onGoHome();
      return;
    case Control::Lut:
      lutIndex = (lutIndex.load() + 1) % LUT_COUNT;
      applyLut();
      fullRedraw = true;
      requestUpdate();
      return;
    case Control::Blink:
      blinkEnabled = !blinkEnabled.load();
      fullRedraw = true;
      requestUpdate();
      return;
    case Control::Clear:
      enqueue(CLEAR_TEXT, now);
      fullRedraw = true;
      requestUpdate();
      return;
    case Control::None:
      break;
  }

  int16_t value = 0;
  if (!keyAt(tx, ty, value)) {
    contactActive = false;
    return;
  }
  blinkUntil = 0;
  enqueue(value, now);
  requestUpdate();
}

const fui::KeyboardLayout& TypingLabActivity::currentLayout() const {
  return fui::builtinKeyboardLayout(layoutId, shifted, symbols, /*numberRow=*/false, showLangKey && !symbols);
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

bool TypingLabActivity::applyKey(const int16_t value) {
  switch (value) {
    case CLEAR_TEXT:
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
  const char* labels[4] = {"< Back", lutLabel, blinkEnabled.load() ? "Blink on" : "Blink off", "Clear"};
  for (int i = 0; i < 4; ++i) {
    const int width = renderer.getTextWidth(UI_12_FONT_ID, labels[i]);
    renderer.drawText(UI_12_FONT_ID, i * quarter + (quarter - width) / 2, MARGIN, labels[i]);
    if (i > 0) renderer.drawLine(i * quarter, MARGIN / 2, i * quarter, bar - MARGIN / 2);
  }
  renderer.drawLine(0, bar - 1, pageWidth - 1, bar - 1);

  char line[112];
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const uint32_t avg = refreshCount ? latencySumMs / refreshCount : 0;
  snprintf(line, sizeof(line), "key %lums  avg %lums  n %lu  panel %lums  cleanups %lu",
           static_cast<unsigned long>(lastLatencyMs), static_cast<unsigned long>(avg),
           static_cast<unsigned long>(refreshCount), static_cast<unsigned long>(lastPanelMs),
           static_cast<unsigned long>(cleanupCount));
  renderer.drawText(SMALL_FONT_ID, MARGIN, bar + MARGIN / 2, line);
  snprintf(line, sizeof(line), "spi %lu  setup %lu  drf %lu  sync %lu",
           static_cast<unsigned long>(freeink::uc8279TimeStream), static_cast<unsigned long>(freeink::uc8279TimeSetup),
           static_cast<unsigned long>(freeink::uc8279TimeDrf), static_cast<unsigned long>(freeink::uc8279TimeSync));
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

void TypingLabActivity::drawKeyboard(const int16_t outline) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::KeyboardLayout& layout = currentLayout();

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
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  fui::keyboard(frame, keyboardRect(), props);
  interactions.publish();

  if (outline == INT16_MIN) return;
  // A thin frame instead of a fill: little ink to erase when the outline goes away.
  const size_t count = interactions.publishedCount();
  const fui::Interaction* items = interactions.publishedData();
  for (size_t i = 0; i < count; ++i) {
    if (items[i].action == ACTION_KEY && items[i].value == outline) {
      const fui::Rect& r = items[i].rect;
      renderer.drawRect(r.x + 2, r.y + 2, r.width - 4, r.height - 4, 2, true);
      return;
    }
  }
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
    keyboardChanged |= applyKey(key.value);
    outlinedKey = key.value == CLEAR_TEXT ? INT16_MIN : key.value;
    ++batch;
    tail = (tail + 1) % QUEUE_SIZE;
  }
  queueTail.store(tail, std::memory_order_release);

  const bool whole = fullRedraw.exchange(false);
  // A lift-off that races ahead of its key frame stays pending for the next frame.
  const bool release = batch == 0 && releaseFrame.exchange(false) && outlinedKey != INT16_MIN;
  const bool cleanup = cleanupFrame.exchange(false) && batch == 0;
  const bool blink = blinkFrame.exchange(false) && batch == 0 && !cleanup;
  if (release || cleanup) outlinedKey = INT16_MIN;
  if (batch > 0) cursorOn = false;  // a moving cursor leaves residue under a short LUT
  if (cleanup || whole) cursorOn = true;
  if (blink) cursorOn = !cursorOn;
  if (!whole && batch == 0 && !release && !cleanup && !blink) return;

  renderer.clearScreen();
  drawBars();
  drawText(cursorOn);
  drawKeyboard(outlinedKey);

  const uint32_t drawDone = millis();
  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    freeink::uc8279FastLutFrames = 0;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    applyLut();
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t panelDone = millis();

  if (batch > 0 || release) lastKeyFrameAt = panelDone;
  if (batch > 0) {
    lastLatencyMs = panelDone - oldestDownMs;
    lastPanelMs = panelDone - drawDone;
    latencySumMs += lastLatencyMs;
    ++refreshCount;
  }
  if (cleanup || whole) {
    lastKeyFrameAt = 0;
    cleanupDoneAt = panelDone;
    if (cleanup) ++cleanupCount;
  }
  if (releaseFrame.load()) requestUpdate();
  if (whole) {
    latencySumMs = 0;
    refreshCount = 0;
    lastLatencyMs = lastPanelMs = 0;
  }
}

#endif  // CPX_TYPING_LAB
