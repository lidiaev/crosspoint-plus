#include "KeyboardEntryActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/plusIcons.h"
#include "components/icons/selectionIcons.h"
#include "components/typing/FastTyping.h"
#include "components/typing/TextSelection.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MARGIN = 14;
constexpr int FONT = UI_12_FONT_ID;
constexpr int ICON = 32;
constexpr uint32_t RESTORE_IDLE_MS = 300;
constexpr uint32_t REPEAT_DELAY_MS = 450;
constexpr uint32_t TYPING_ACTIVE_MS = 2000;  // full-speed loop after the last touch
constexpr uint32_t ALTERNATE_DELAY_MS = 450;
constexpr uint32_t LONG_PRESS_MS = 500;
constexpr int HOLD_SLOP = 16;
constexpr const char* MASK = "•";

size_t codePointBytes(const std::string& s, const size_t i) {
  const uint8_t c = static_cast<uint8_t>(s[i]);
  if (c < 0x80) return 1;
  if (c < 0xE0) return 2;
  if (c < 0xF0) return 3;
  return 4;
}
}  // namespace

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  keyboard.begin(inputType == InputType::Url || TypingKeyboard::startsInEnglish());
  keyboard.setReturnKey(inputType == InputType::Search ? TypingKeyboard::ReturnKey::Search
                                                       : TypingKeyboard::ReturnKey::Done);
  lineStarts.reserve(8);
  cursor = text.size();
  fast_typing::enable();
  fullRedraw = true;
  requestUpdate();
}

// Called with the render lock held: the render task is idle.
void KeyboardEntryActivity::onExit() {
  fast_typing::disable();
  Activity::onExit();
}

void KeyboardEntryActivity::enqueue(const Event& event) {
  const uint8_t head = queueHead.load(std::memory_order_relaxed);
  const uint8_t next = (head + 1) % QUEUE_SIZE;
  if (next == queueTail.load(std::memory_order_acquire)) return;
  queue[head] = event;
  queueHead.store(next, std::memory_order_release);
}

bool KeyboardEntryActivity::popEvent(Event& event) {
  const uint8_t tail = queueTail.load(std::memory_order_relaxed);
  if (tail == queueHead.load(std::memory_order_acquire)) return false;
  event = queue[tail];
  queueTail.store((tail + 1) % QUEUE_SIZE, std::memory_order_release);
  return true;
}

void KeyboardEntryActivity::loop() {
  if (done.load()) {
    std::string result;
    {
      RenderLock lock;
      result = text;
    }
    onComplete(std::move(result));
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  const uint32_t now = millis();
  const bool queueEmpty = queueTail.load() == queueHead.load();
  bool update = false;

  const uint32_t shownAt = pressedShownAt.load();
  if (shownAt != 0 && !touch.anyDown() && queueEmpty && now - std::max(shownAt, liftedAt) > RESTORE_IDLE_MS &&
      !releaseFrame.load()) {
    releaseFrame = true;
    update = true;
  }
  const uint32_t editAt = lastEditFrameAt.load();
  if (editAt != 0 && now - editAt > fast_typing::cleanupDelayMs() && queueEmpty && !touch.anyDown() &&
      !cleanupFrame.load()) {
    cleanupFrame = true;
    update = true;
  }

  TouchTracker::Press presses[4];
  const uint8_t count = touch.poll(renderer, now, presses, 4);
  if (count > 0 || touch.anyDown()) lastInputAt = now;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  for (uint8_t i = 0; i < count; ++i) {
    const TouchTracker::Press& p = presses[i];
    if (p.y < headerBottom) {
      // The back arrow reaches loop() as Button::Back (HeaderBackTapTarget).
      if (doneW > 0 && p.x >= doneX - MARGIN) {
        enqueue(Event{EventType::Done, 0, p.downMs});
        update = true;
      }
      continue;
    }
    if (p.x >= calloutLeft && p.x < calloutRight && p.y >= calloutTop && p.y < calloutBottom) {
      enqueue(Event{EventType::Callout, 0, p.downMs, p.x, p.y});
      update = true;
      continue;
    }
    int16_t value = 0;
    if (keyboard.keyAt(p.x, p.y, value)) {
      touch.setPressedValue(touch.pressSlot(i), value);
      if (value == fui::QWERTY_KEY_BACKSPACE) {
        repeatFor = p.downMs;
        nextRepeatAt = p.downMs + REPEAT_DELAY_MS;
      }
      if (TypingKeyboard::alternateOf(value) != 0) alternateFor = p.downMs;
      enqueue(Event{EventType::Key, value, p.downMs});
      update = true;
    } else if (p.y >= fieldTop && p.y < fieldBottom) {
      const bool onEye = inputType == InputType::Password && p.x >= renderer.getScreenWidth() - MARGIN - ICON - MARGIN;
      enqueue(Event{onEye ? EventType::TogglePassword : EventType::TapField, 0, p.downMs, p.x, p.y});
      update = true;
      if (!onEye) {
        holdSlot = static_cast<int8_t>(touch.pressSlot(i));
        holdDownMs = p.downMs;
        holdX = p.x;
        holdY = p.y;
      }
    }
  }

  if (holdSlot >= 0) {
    int16_t x = 0;
    int16_t y = 0;
    if (!touch.position(static_cast<uint8_t>(holdSlot), holdDownMs, x, y) || std::abs(x - holdX) >= HOLD_SLOP ||
        std::abs(y - holdY) >= HOLD_SLOP) {
      holdSlot = -1;
    } else if (now - holdDownMs >= LONG_PRESS_MS) {
      enqueue(Event{EventType::HoldField, 0, now, holdX, holdY});
      holdSlot = -1;
      update = true;
    }
  }

  TouchTracker::Held held{};
  if (touch.heldValue(fui::QWERTY_KEY_BACKSPACE, held) && held.downMs == repeatFor && now >= nextRepeatAt) {
    enqueue(Event{EventType::Key, fui::QWERTY_KEY_BACKSPACE, now});
    nextRepeatAt = now + (now - held.downMs > 1500 ? 50 : 110);
    update = true;
  }
  if (alternateFor != 0) {
    bool still = false;
    for (const int16_t base : {int16_t{0x0435}, int16_t{0x0415}}) {
      if (touch.heldValue(base, held) && held.downMs == alternateFor) {
        still = true;
        if (now - held.downMs >= ALTERNATE_DELAY_MS) {
          enqueue(Event{EventType::Alternate, base, now});
          alternateFor = 0;
          update = true;
        }
      }
    }
    if (!still) alternateFor = 0;
  }

  if (wasDown && !touch.anyDown()) liftedAt = now;
  wasDown = touch.anyDown();
  if (update) requestUpdate();
}

// The main loop spins without pauses only while typing; between bursts it may
// idle and lower the CPU clock. Pending idle frames keep it awake until drawn.
bool KeyboardEntryActivity::skipLoopDelay() {
  return touch.anyDown() || millis() - lastInputAt < TYPING_ACTIVE_MS || queueTail.load() != queueHead.load() ||
         releaseFrame.load() || cleanupFrame.load() || pressedShownAt.load() != 0 || lastEditFrameAt.load() != 0;
}

// --- render task ---------------------------------------------------------------

void KeyboardEntryActivity::insertText(const char* utf8) {
  const size_t len = strlen(utf8);
  if (maxLength != 0 && text.size() + len > maxLength) return;
  text.insert(cursor, utf8, len);
  cursor += len;
}

void KeyboardEntryActivity::deleteBackward() {
  if (cursor == 0) return;
  size_t i = cursor - 1;
  while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i, cursor - i);
  cursor = i;
}

std::string KeyboardEntryActivity::shownText() const {
  if (inputType != InputType::Password || passwordVisible) return text;
  std::string shown;
  shown.reserve(text.size() * 3);
  for (size_t i = 0; i < text.size(); i += codePointBytes(text, i)) shown += MASK;
  return shown;
}

// A masked password draws every code point as one three-byte "•".
size_t KeyboardEntryActivity::toShown(const size_t offset) const {
  if (inputType != InputType::Password || passwordVisible) return offset;
  size_t points = 0;
  for (size_t i = 0; i < offset && i < text.size(); i += codePointBytes(text, i)) ++points;
  return points * strlen(MASK);
}

size_t KeyboardEntryActivity::fromShown(const size_t offset) const {
  if (inputType != InputType::Password || passwordVisible) return offset;
  size_t points = offset / strlen(MASK);
  size_t i = 0;
  while (i < text.size() && points-- > 0) i += codePointBytes(text, i);
  return i;
}

void KeyboardEntryActivity::wrapField(const std::string& shown) {
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN - (inputType == InputType::Password ? ICON + MARGIN : 0);
  lineStarts.clear();
  lineStarts.push_back(0);
  int lineWidth = 0;
  char glyph[5] = {};
  for (size_t i = 0; i < shown.size();) {
    const size_t len = std::min(codePointBytes(shown, i), shown.size() - i);
    memcpy(glyph, shown.data() + i, len);
    glyph[len] = '\0';
    const int w = renderer.getTextAdvanceX(FONT, glyph, EpdFontFamily::REGULAR);
    if (lineWidth + w > maxWidth && lineWidth > 0) {
      lineStarts.push_back(static_cast<uint32_t>(i));
      lineWidth = 0;
    }
    lineWidth += w;
    i += len;
  }
}

// A tap on the text puts the cursor on the nearest character boundary.
void KeyboardEntryActivity::placeCursor(const int x, const int y) {
  const std::string shown = shownText();
  wrapField(shown);
  const int lineHeight = renderer.getLineHeight(FONT);
  const int lines = static_cast<int>(lineStarts.size());
  const int line = std::clamp(firstLine + (y - fieldTop - MARGIN / 2) / lineHeight, 0, lines - 1);
  size_t i = lineStarts[line];
  const size_t end = line + 1 < lines ? lineStarts[line + 1] : shown.size();
  int penX = MARGIN;
  char glyph[5] = {};
  while (i < end) {
    const size_t len = std::min(codePointBytes(shown, i), shown.size() - i);
    memcpy(glyph, shown.data() + i, len);
    glyph[len] = '\0';
    const int w = renderer.getTextAdvanceX(FONT, glyph, EpdFontFamily::REGULAR);
    if (x < penX + w / 2) break;
    penX += w;
    i += len;
  }
  cursor = fromShown(i);
}

void KeyboardEntryActivity::handleEvent(const Event& event) {
  switch (event.type) {
    case EventType::Key: {
      char out[4] = {};
      const TypingKeyboard::Action action = keyboard.press(event.value, event.downMs, out);
      if (action == TypingKeyboard::Action::Insert || action == TypingKeyboard::Action::Backspace) {
        editOffer = false;
        // Typing over Select all replaces the text; Backspace clears it.
        if (allSelected) {
          allSelected = false;
          text.clear();
          cursor = 0;
          if (action == TypingKeyboard::Action::Backspace) break;
        }
      }
      switch (action) {
        case TypingKeyboard::Action::Insert:
          insertText(out);
          break;
        case TypingKeyboard::Action::Backspace:
          deleteBackward();
          break;
        case TypingKeyboard::Action::Enter:
          done = true;
          break;
        default:
          break;
      }
      pressedKey = event.value;
      break;
    }
    case EventType::Alternate: {
      // Replaces the е just typed with ё.
      TypingKeyboard::replaceWithAlternate(text, cursor);
      break;
    }
    case EventType::TogglePassword:
      passwordVisible = !passwordVisible;
      break;
    case EventType::TapField:
      allSelected = editOffer = false;
      placeCursor(event.x, event.y);
      break;
    case EventType::HoldField:
      editOffer = true;
      break;
    case EventType::Callout: {
      Action list[MAX_ACTIONS];
      const int n = actions(list);
      const int item = callout.itemAt(event.x, event.y);
      if (item >= 0 && item < n) applyAction(list[item]);
      break;
    }
    case EventType::Done:
      done = true;
      break;
  }
}

// The text wraps under the header and scrolls to keep the cursor in view.
void KeyboardEntryActivity::drawField() {
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(FONT);
  const std::string shown = shownText();
  wrapField(shown);

  const int top = fieldTop + MARGIN / 2;
  const int maxLines = std::max(1, (keyboard.rect(renderer).y - MARGIN - top) / lineHeight - 1);
  const int lines = static_cast<int>(lineStarts.size());
  const size_t caret = toShown(cursor);
  int cursorLine = lines - 1;
  while (cursorLine > 0 && lineStarts[cursorLine] > caret) --cursorLine;
  firstLine = std::clamp(firstLine, std::max(0, cursorLine - maxLines + 1), cursorLine);
  firstLine = std::min(firstLine, std::max(0, lines - maxLines));
  const int last = std::min(lines, firstLine + maxLines);

  int y = top;
  std::string segment;
  int caretX = MARGIN;
  int caretTop = top;
  int lastX = MARGIN;
  for (int l = firstLine; l < last; ++l) {
    const size_t start = lineStarts[l];
    const size_t end = l + 1 < lines ? lineStarts[l + 1] : shown.size();
    segment.assign(shown, start, end - start);
    // Select all draws the text white on black, without the cursor.
    const int segmentWidth = renderer.getTextAdvanceX(FONT, segment.c_str(), EpdFontFamily::REGULAR);
    if (allSelected && !segment.empty()) renderer.fillRect(MARGIN, y, segmentWidth, lineHeight);
    renderer.drawText(FONT, MARGIN, y, segment.c_str(), !allSelected);
    lastX = MARGIN + segmentWidth;
    if (l == cursorLine) {
      // Measured as drawn (with kerning) and centered on the boundary.
      segment.resize(caret - start);
      caretX = MARGIN + renderer.getTextAdvanceX(FONT, segment.c_str(), EpdFontFamily::REGULAR);
      caretTop = y;
      if (!allSelected) renderer.fillRect(caretX - 1, y, 2, lineHeight);
    }
    y += lineHeight;
  }

  if (inputType == InputType::Password) {
    const freeink::Icon& icon = passwordVisible ? icon_hide_password_32 : icon_show_password_32;
    renderer.drawIconUpright(icon.bits, width - MARGIN - ICON, top + (lineHeight - ICON) / 2, ICON, ICON);
  }
  fieldBottom = y + MARGIN / 2;
  renderer.drawLine(MARGIN, fieldBottom, width - MARGIN, fieldBottom);
  if (allSelected) {
    drawCallout(MARGIN, top, lastX, y);
  } else {
    drawCallout(caretX, caretTop, caretX, caretTop + lineHeight);
  }
}

// The callout's actions, left to right; none while it is closed. A hidden
// password is never copied.
int KeyboardEntryActivity::actions(Action out[MAX_ACTIONS]) const {
  int n = 0;
  const bool copyable = inputType != InputType::Password || passwordVisible;
  if (allSelected) {
    if (copyable) out[n++] = Action::Copy;
    if (copyable) out[n++] = Action::Cut;
    if (!clipboard::empty()) out[n++] = Action::Paste;
  } else if (editOffer) {
    if (!text.empty()) out[n++] = Action::SelectAll;
    if (!clipboard::empty()) out[n++] = Action::Paste;
  }
  return n;
}

void KeyboardEntryActivity::applyAction(const Action action) {
  switch (action) {
    case Action::Copy:
      clipboard::set(text);
      allSelected = false;
      break;
    case Action::Cut:
      clipboard::set(text);
      text.clear();
      cursor = 0;
      allSelected = false;
      break;
    case Action::Paste: {
      if (allSelected) {
        text.clear();
        cursor = 0;
        allSelected = false;
      }
      // Fields hold one paragraph: line breaks become spaces.
      std::string pasted = clipboard::get();
      std::replace(pasted.begin(), pasted.end(), '\n', ' ');
      insertText(pasted.c_str());
      break;
    }
    case Action::SelectAll:
      allSelected = true;
      cursor = text.size();
      break;
  }
  editOffer = false;
}

void KeyboardEntryActivity::drawCallout(const int startX, const int startTop, const int endX, const int endBottom) {
  Action list[MAX_ACTIONS];
  const int n = actions(list);
  callout.place(n, startX, startTop, endX, endBottom, fieldTop, keyboard.rect(renderer).y, renderer.getScreenWidth());
  const freeink::Icon* icons[MAX_ACTIONS] = {};
  for (int i = 0; i < n; ++i) {
    switch (list[i]) {
      case Action::Copy:
        icons[i] = &icon_sel_copy_24;
        break;
      case Action::Cut:
        icons[i] = &icon_sel_cut_24;
        break;
      case Action::Paste:
        icons[i] = &icon_sel_paste_24;
        break;
      case Action::SelectAll:
        icons[i] = &icon_sel_select_all_24;
        break;
    }
  }
  callout.draw(renderer, icons);
  callout.bounds(calloutLeft, calloutTop, calloutRight, calloutBottom);
}

void KeyboardEntryActivity::render(RenderLock&&) {
  if (done.load()) return;
  int keyEvents = 0;
  int events = 0;
  Event event{};
  while (!done.load() && popEvent(event)) {
    if (event.type == EventType::Key || event.type == EventType::Alternate || event.type == EventType::Callout) {
      ++keyEvents;
    }
    ++events;
    handleEvent(event);
  }
  if (done.load()) return;

  const bool whole = fullRedraw.exchange(false);
  const bool release = keyEvents == 0 && releaseFrame.exchange(false) && pressedKey != INT16_MIN;
  const bool cleanup = keyEvents == 0 && cleanupFrame.exchange(false);
  if (release || cleanup) pressedKey = INT16_MIN;
  if (!whole && events == 0 && !release && !cleanup) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  const Rect doneRect =
      GUI.drawHeaderWithAction(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                               title.c_str(), icon_done_32);
  doneX = doneRect.x;
  doneW = doneRect.width;
  fieldTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  drawField();
  keyboard.draw(renderer, pressedKey);

  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    fast_typing::CleanRefreshScope clean;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t finished = millis();
  if (events > 0 || release) lastEditFrameAt = finished;
  if (cleanup || whole) lastEditFrameAt = 0;
  pressedShownAt = pressedKey != INT16_MIN ? finished : 0;
  if (releaseFrame.load()) requestUpdate();
}

void KeyboardEntryActivity::onComplete(std::string result) {
  setResult(KeyboardResult{std::move(result)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
