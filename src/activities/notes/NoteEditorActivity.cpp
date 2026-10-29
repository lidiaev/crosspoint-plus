#include "NoteEditorActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "FastTyping.h"
#include "MappedInputManager.h"
#include "NotesListActivity.h"
#include "NotesStore.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MARGIN = 14;
constexpr int TEXT_FONT = UI_12_FONT_ID;
constexpr uint32_t CLEANUP_IDLE_MS = 1200;  // idle repaint + autosave after this pause
constexpr uint32_t RESTORE_IDLE_MS = 300;   // the pressed key returns to normal after this pause
constexpr uint32_t REPEAT_DELAY_MS = 450;

size_t decode(const std::string& s, size_t i, uint32_t& cp) {
  const uint8_t c = static_cast<uint8_t>(s[i]);
  if (c < 0x80 || i + 1 >= s.size()) {
    cp = c;
    return 1;
  }
  if (c < 0xE0 || i + 2 >= s.size()) {
    cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F);
    return 2;
  }
  cp = ((c & 0x0F) << 12) | ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(s[i + 2]) & 0x3F);
  return 3;
}
}  // namespace

void NoteEditorActivity::onEnter() {
  Activity::onEnter();
  if (!path.empty() && !notes::load(path, text)) {
    LOG_ERR("NOTES", "Cannot open %s", path.c_str());
  }
  cursor = text.size();
  lineStarts.reserve(64);
  keyboard.begin(keyboard_layouts::startingLayout() == fui::KeyboardLayoutId::QwertyEn);
  keyboard.autoCapitalize(text, cursor);
  fast_typing::enable();
  fullRedraw = true;
  requestUpdate();
}

void NoteEditorActivity::onExit() {
  {
    RenderLock lock;
    saveNow();
    // Keep the file name in step with the first line.
    if (!path.empty()) {
      const std::string desired = notes::pathFor(text, path);
      if (desired != path && notes::save(desired, text)) {
        notes::remove(path);
        path = desired;
      }
    }
  }
  fast_typing::disable();
  Activity::onExit();
}

void NoteEditorActivity::enqueue(const Event& event) {
  const uint8_t head = queueHead.load(std::memory_order_relaxed);
  const uint8_t next = (head + 1) % QUEUE_SIZE;
  if (next == queueTail.load(std::memory_order_acquire)) return;  // full: drop
  queue[head] = event;
  queueHead.store(next, std::memory_order_release);
}

bool NoteEditorActivity::popEvent(Event& event) {
  const uint8_t tail = queueTail.load(std::memory_order_relaxed);
  if (tail == queueHead.load(std::memory_order_acquire)) return false;
  event = queue[tail];
  queueTail.store((tail + 1) % QUEUE_SIZE, std::memory_order_release);
  return true;
}

int NoteEditorActivity::headerHeight() const { return renderer.getLineHeight(UI_12_FONT_ID) + 2 * MARGIN; }

int NoteEditorActivity::textTop() const { return headerHeight() + MARGIN / 2; }

int NoteEditorActivity::textBottom() const {
  return keyboardShown.load() ? keyboard.rect(renderer).y - MARGIN / 2 : renderer.getScreenHeight() - MARGIN;
}

int NoteEditorActivity::visibleLines() const {
  return std::max(1, (textBottom() - textTop()) / renderer.getLineHeight(TEXT_FONT));
}

void NoteEditorActivity::loop() {
  if (exitRequested.load()) {
    activityManager.replaceActivity(std::make_unique<NotesListActivity>(renderer, mappedInput));
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
    return;
  }

  const uint32_t now = millis();
  const bool queueEmpty = queueTail.load() == queueHead.load();
  bool update = false;

  // Scrolling: side buttons always, swipes over the text when the keyboard is hidden.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    enqueue(Event{EventType::ScrollUp, 0, 0, 0, now});
    update = true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    enqueue(Event{EventType::ScrollDown, 0, 0, 0, now});
    update = true;
  }
  if (!keyboardShown.load()) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      enqueue(
          Event{swipe == MappedInputManager::SwipeDir::Up ? EventType::ScrollDown : EventType::ScrollUp, 0, 0, 0, now});
      update = true;
    }
  }

  // Idle work: return the pressed key to normal, then clear residue and save.
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
  const int width = renderer.getScreenWidth();
  for (uint8_t i = 0; i < count; ++i) {
    const TouchTracker::Press& p = presses[i];
    if (p.y < headerHeight()) {
      if (p.x < width / 3) {
        exitRequested = true;
        return;
      }
      if (p.x >= 2 * width / 3) {
        enqueue(Event{EventType::ToggleKeyboard, 0, p.x, p.y, p.downMs});
        update = true;
      }
      continue;
    }
    int16_t value = 0;
    if (keyboardShown.load() && keyboard.keyAt(p.x, p.y, value)) {
      touch.setPressedValue(touch.pressSlot(i), value);
      if (value == fui::QWERTY_KEY_BACKSPACE) {
        repeatFor = p.downMs;
        nextRepeatAt = p.downMs + REPEAT_DELAY_MS;
      }
      enqueue(Event{EventType::Key, value, p.x, p.y, p.downMs});
      update = true;
    } else if (p.y >= textTop() && p.y < textBottom()) {
      enqueue(Event{EventType::TapText, 0, p.x, p.y, p.downMs});
      update = true;
    }
  }

  // Holding Backspace repeats, faster the longer it is held.
  TouchTracker::Held held{};
  if (touch.heldValue(fui::QWERTY_KEY_BACKSPACE, held) && held.downMs == repeatFor && now >= nextRepeatAt) {
    enqueue(Event{EventType::Key, fui::QWERTY_KEY_BACKSPACE, held.x, held.y, now});
    nextRepeatAt = now + (now - held.downMs > 1500 ? 50 : 110);
    update = true;
  }

  if (wasDown && !touch.anyDown()) liftedAt = now;
  wasDown = touch.anyDown();
  if (update) requestUpdate();
}

// --- render task ---------------------------------------------------------------

int NoteEditorActivity::advance(const uint32_t cp) {
  if (cp < sizeof(widthCache) && widthCache[cp] != 0) return widthCache[cp];
  char buf[4] = {};
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  const int w = std::max(1, renderer.getTextAdvanceX(TEXT_FONT, buf, EpdFontFamily::REGULAR));
  if (cp < sizeof(widthCache)) widthCache[cp] = static_cast<uint8_t>(std::min(w, 255));
  return w;
}

// Word wrap: break after the last space that fits; a word wider than the line breaks anywhere.
void NoteEditorActivity::wrap() {
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN;
  lineStarts.clear();
  lineStarts.push_back(0);
  size_t lineStart = 0;
  size_t lastBreak = std::string::npos;  // offset just after the last space on this line
  int width = 0;
  int widthSinceBreak = 0;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = 0;
    const size_t len = decode(text, i, cp);
    if (cp == '\n') {
      i += len;
      lineStarts.push_back(static_cast<uint32_t>(i));
      lineStart = i;
      lastBreak = std::string::npos;
      width = widthSinceBreak = 0;
      continue;
    }
    const int w = advance(cp);
    if (width + w > maxWidth && i > lineStart && cp != ' ') {
      if (lastBreak != std::string::npos && lastBreak > lineStart) {
        lineStarts.push_back(static_cast<uint32_t>(lastBreak));
        lineStart = lastBreak;
        width = widthSinceBreak;
      } else {
        lineStarts.push_back(static_cast<uint32_t>(i));
        lineStart = i;
        width = 0;
      }
      lastBreak = std::string::npos;
      widthSinceBreak = 0;
    }
    width += w;
    widthSinceBreak += w;
    i += len;
    if (cp == ' ') {
      lastBreak = i;
      widthSinceBreak = 0;
    }
  }
}

int NoteEditorActivity::lineOf(const size_t offset) const {
  const auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), static_cast<uint32_t>(offset));
  return std::max(0, static_cast<int>(it - lineStarts.begin()) - 1);
}

void NoteEditorActivity::keepCursorVisible() {
  const int line = lineOf(cursor);
  const int visible = visibleLines();
  if (line < topLine) topLine = line;
  if (line >= topLine + visible) topLine = line - visible + 1;
}

void NoteEditorActivity::insertText(const char* utf8) {
  const size_t len = strlen(utf8);
  if (text.size() + len > notes::MAX_NOTE_BYTES) return;
  text.insert(cursor, utf8, len);
  cursor += len;
  dirty = true;
}

void NoteEditorActivity::deleteBackward() {
  if (cursor == 0) return;
  size_t i = cursor - 1;
  while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i, cursor - i);
  cursor = i;
  dirty = true;
}

void NoteEditorActivity::applyKey(const int16_t value, const uint32_t downMs, bool& layoutChanged) {
  char out[4] = {};
  switch (keyboard.press(value, downMs, out)) {
    case TypingKeyboard::Action::Insert:
      // Double space after a word ends the sentence.
      if (out[0] == ' ' && cursor >= 2 && text[cursor - 1] == ' ' && text[cursor - 2] != ' ' &&
          text[cursor - 2] != '.' && text[cursor - 2] != '\n') {
        text[cursor - 1] = '.';
      }
      insertText(out);
      break;
    case TypingKeyboard::Action::Backspace:
      deleteBackward();
      break;
    case TypingKeyboard::Action::Enter:
      insertText("\n");
      break;
    case TypingKeyboard::Action::LayoutChanged:
      layoutChanged = true;
      break;
    case TypingKeyboard::Action::None:
      break;
  }
  if (keyboard.autoCapitalize(text, cursor)) layoutChanged = true;
}

void NoteEditorActivity::placeCursor(const int x, const int y) {
  const int lineHeight = renderer.getLineHeight(TEXT_FONT);
  const int line = std::clamp(topLine + (y - textTop()) / lineHeight, 0, static_cast<int>(lineStarts.size()) - 1);
  size_t i = lineStarts[line];
  const size_t end = line + 1 < static_cast<int>(lineStarts.size()) ? lineStarts[line + 1] : text.size();
  int penX = MARGIN;
  while (i < end) {
    uint32_t cp = 0;
    const size_t len = decode(text, i, cp);
    if (cp == '\n') break;
    const int w = advance(cp);
    if (x < penX + w / 2) break;
    penX += w;
    i += len;
  }
  // A wrapped line ends in the space it broke after: keep the cursor before it.
  if (i == end && end < text.size() && i > lineStarts[line] && text[i - 1] == ' ') --i;
  cursor = i;
  keyboard.autoCapitalize(text, cursor);
}

void NoteEditorActivity::handleEvent(const Event& event, bool& layoutChanged) {
  switch (event.type) {
    case EventType::Key:
      applyKey(event.value, event.downMs, layoutChanged);
      pressedKey = event.value;
      keepCursorVisible();
      break;
    case EventType::TapText:
      placeCursor(event.x, event.y);
      if (!keyboardShown.load()) {
        keyboardShown = true;
        fullRedraw = true;
      }
      keepCursorVisible();
      break;
    case EventType::ToggleKeyboard:
      keyboardShown = !keyboardShown.load();
      fullRedraw = true;
      keepCursorVisible();
      break;
    case EventType::ScrollUp:
      topLine = std::max(0, topLine - (visibleLines() - 1));
      break;
    case EventType::ScrollDown:
      topLine =
          std::min(std::max(0, static_cast<int>(lineStarts.size()) - visibleLines()), topLine + visibleLines() - 1);
      break;
    case EventType::Back:
      break;
  }
}

void NoteEditorActivity::saveNow() {
  if (!dirty) return;
  if (notes::titleOf(text).empty()) {
    // Nothing worth keeping: an empty note leaves no file behind.
    if (!path.empty()) notes::remove(path);
    path.clear();
    dirty = false;
    return;
  }
  if (path.empty()) path = notes::pathFor(text, "");
  if (notes::save(path, text)) dirty = false;
}

void NoteEditorActivity::drawHeader() const {
  const int width = renderer.getScreenWidth();
  const int bar = headerHeight();
  renderer.drawText(UI_12_FONT_ID, MARGIN, MARGIN, "< ");
  const int arrow = renderer.getTextWidth(UI_12_FONT_ID, "< ");
  renderer.drawText(UI_12_FONT_ID, MARGIN + arrow, MARGIN, tr(STR_NOTES));
  const char* toggle = keyboardShown.load() ? tr(STR_HIDE_KEYBOARD) : tr(STR_SHOW_KEYBOARD);
  const int tw = renderer.getTextWidth(SMALL_FONT_ID, toggle);
  const int sh = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, width - MARGIN - tw, (bar - sh) / 2, toggle);
  renderer.drawLine(0, bar - 1, width - 1, bar - 1);
}

void NoteEditorActivity::drawText() {
  const int lineHeight = renderer.getLineHeight(TEXT_FONT);
  const int lines = static_cast<int>(lineStarts.size());
  const int last = std::min(lines, topLine + visibleLines());
  const int cursorLine = lineOf(cursor);
  std::string segment;
  int y = textTop();
  for (int l = topLine; l < last; ++l) {
    const size_t start = lineStarts[l];
    size_t end = l + 1 < lines ? lineStarts[l + 1] : text.size();
    if (end > start && text[end - 1] == '\n') --end;
    segment.assign(text, start, end - start);
    renderer.drawText(TEXT_FONT, MARGIN, y, segment.c_str());
    if (l == cursorLine) {
      int x = MARGIN;
      for (size_t i = start; i < cursor;) {
        uint32_t cp = 0;
        const size_t len = decode(text, i, cp);
        x += advance(cp);
        i += len;
      }
      renderer.fillRect(x, y, 2, lineHeight);
    }
    y += lineHeight;
  }
}

void NoteEditorActivity::render(RenderLock&&) {
  bool layoutChanged = false;
  int keyEvents = 0;
  int events = 0;
  Event event{};
  while (popEvent(event)) {
    if (event.type == EventType::Key) ++keyEvents;
    ++events;
    handleEvent(event, layoutChanged);
  }

  const bool whole = fullRedraw.exchange(false);
  const bool release = keyEvents == 0 && releaseFrame.exchange(false) && pressedKey != INT16_MIN;
  const bool cleanup = keyEvents == 0 && cleanupFrame.exchange(false);
  if (release || cleanup) pressedKey = INT16_MIN;
  if (cleanup) saveNow();
  if (!whole && events == 0 && !release && !cleanup) return;

  wrap();
  topLine = std::clamp(topLine, 0, std::max(0, static_cast<int>(lineStarts.size()) - 1));

  renderer.clearScreen();
  drawHeader();
  drawText();
  if (keyboardShown.load()) keyboard.draw(renderer, pressedKey);

  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    fast_typing::CleanRefreshScope clean;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t done = millis();

  if (keyEvents > 0 || release) lastEditFrameAt = done;
  if (cleanup || whole) lastEditFrameAt = 0;
  pressedShownAt = pressedKey != INT16_MIN ? done : 0;
  if (releaseFrame.load()) requestUpdate();
}
