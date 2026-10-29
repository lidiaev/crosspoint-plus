#include "KeyboardEntryActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "KeyboardLayoutSet.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/notesIcons.h"
#include "components/typing/FastTyping.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MARGIN = 14;
constexpr int FONT = UI_12_FONT_ID;
constexpr int ICON = 32;
constexpr uint32_t CLEANUP_IDLE_MS = 1200;
constexpr uint32_t RESTORE_IDLE_MS = 300;
constexpr uint32_t REPEAT_DELAY_MS = 450;
constexpr uint32_t ALTERNATE_DELAY_MS = 450;
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
  keyboard.begin(inputType == InputType::Url || keyboard_layouts::startingLayout() == fui::KeyboardLayoutId::QwertyEn);
  lineStarts.reserve(8);
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
  if (editAt != 0 && now - editAt > CLEANUP_IDLE_MS && queueEmpty && !touch.anyDown() && !cleanupFrame.load()) {
    cleanupFrame = true;
    update = true;
  }

  TouchTracker::Press presses[4];
  const uint8_t count = touch.poll(renderer, now, presses, 4);
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
    } else if (inputType == InputType::Password && p.y >= fieldTop && p.y < fieldBottom &&
               p.x >= renderer.getScreenWidth() - MARGIN - ICON - MARGIN) {
      enqueue(Event{EventType::TogglePassword, 0, p.downMs});
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

// --- render task ---------------------------------------------------------------

void KeyboardEntryActivity::insertText(const char* utf8) {
  const size_t len = strlen(utf8);
  if (maxLength != 0 && text.size() + len > maxLength) return;
  text.append(utf8, len);
}

void KeyboardEntryActivity::deleteBackward() {
  if (text.empty()) return;
  size_t i = text.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i);
}

void KeyboardEntryActivity::handleEvent(const Event& event) {
  switch (event.type) {
    case EventType::Key: {
      char out[4] = {};
      switch (keyboard.press(event.value, event.downMs, out)) {
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
      TypingKeyboard::replaceWithAlternate(text, text.size());
      break;
    }
    case EventType::TogglePassword:
      passwordVisible = !passwordVisible;
      break;
    case EventType::Done:
      done = true;
      break;
  }
}

// The text wraps under the header; when it outgrows the space above the
// keyboard, its last lines stay in view. The cursor always sits at the end.
void KeyboardEntryActivity::drawField() {
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(FONT);
  const bool masked = inputType == InputType::Password && !passwordVisible;
  const int maxWidth = width - 2 * MARGIN - (inputType == InputType::Password ? ICON + MARGIN : 0);

  std::string shown;
  if (masked) {
    shown.reserve(text.size());
    for (size_t i = 0; i < text.size(); i += codePointBytes(text, i)) shown += MASK;
  }
  const std::string& s = masked ? shown : text;

  lineStarts.clear();
  lineStarts.push_back(0);
  int lineWidth = 0;
  char glyph[5] = {};
  for (size_t i = 0; i < s.size();) {
    const size_t len = std::min(codePointBytes(s, i), s.size() - i);
    memcpy(glyph, s.data() + i, len);
    glyph[len] = '\0';
    const int w = renderer.getTextAdvanceX(FONT, glyph, EpdFontFamily::REGULAR);
    if (lineWidth + w > maxWidth && lineWidth > 0) {
      lineStarts.push_back(static_cast<uint32_t>(i));
      lineWidth = 0;
    }
    lineWidth += w;
    i += len;
  }

  const int top = fieldTop + MARGIN / 2;
  const int maxLines = std::max(1, (keyboard.rect(renderer).y - MARGIN - top) / lineHeight - 1);
  const int lines = static_cast<int>(lineStarts.size());
  const int first = std::max(0, lines - maxLines);
  int y = top;
  std::string segment;
  for (int l = first; l < lines; ++l) {
    const size_t start = lineStarts[l];
    const size_t end = l + 1 < lines ? lineStarts[l + 1] : s.size();
    segment.assign(s, start, end - start);
    renderer.drawText(FONT, MARGIN, y, segment.c_str());
    if (l == lines - 1) renderer.fillRect(MARGIN + lineWidth, y, 2, lineHeight);
    y += lineHeight;
  }

  if (inputType == InputType::Password) {
    const freeink::Icon& icon = passwordVisible ? icon_hide_password_32 : icon_show_password_32;
    renderer.drawIcon(icon.bits, width - MARGIN - ICON, top + (lineHeight - ICON) / 2, ICON);
  }
  fieldBottom = y + MARGIN / 2;
  renderer.drawLine(MARGIN, fieldBottom, width - MARGIN, fieldBottom);
}

void KeyboardEntryActivity::render(RenderLock&&) {
  if (done.load()) return;
  int keyEvents = 0;
  int events = 0;
  Event event{};
  while (!done.load() && popEvent(event)) {
    if (event.type == EventType::Key || event.type == EventType::Alternate) ++keyEvents;
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
