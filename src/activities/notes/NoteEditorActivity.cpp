#include "NoteEditorActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "MappedInputManager.h"
#include "NoteMarkdown.h"
#include "NotesListActivity.h"
#include "NotesStore.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "components/icons/keyboardIcons.h"
#include "components/icons/notesIcons.h"
#include "components/typing/FastTyping.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MARGIN = 14;
constexpr int BOX_GAP = 10;  // after a checkbox
constexpr int QUOTE_WIDTH = 14;
constexpr int CODE_PAD = 6;                 // a backtick's width: room for the frame around code
constexpr uint32_t CLEANUP_IDLE_MS = 1200;  // idle repaint + autosave after this pause
constexpr uint32_t RESTORE_IDLE_MS = 300;   // the pressed key returns to normal after this pause
constexpr uint32_t REPEAT_DELAY_MS = 450;
constexpr int TOOLBAR_HEIGHT = 52;            // format buttons above the keyboard
constexpr uint32_t TYPING_ACTIVE_MS = 2000;   // full-speed loop after the last touch
constexpr uint32_t ALTERNATE_DELAY_MS = 450;  // long press on е types ё
constexpr int DRAG_SLOP = 16;                 // px a finger moves before a tap becomes a drag

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
  // A new note opens ready to type; an existing one opens for reading.
  keyboardShown = path.empty();
  lineStarts.reserve(64);
  keyboard.begin(TypingKeyboard::startsInEnglish());
  keyboard.autoCapitalize(text, cursor);
  fast_typing::enable();
  fullRedraw = true;
  requestUpdate();
}

// ActivityManager calls onExit with the render lock held, so the render task is
// idle here; taking the lock again would deadlock.
void NoteEditorActivity::onExit() {
  saveNow();
  // Keep the file name in step with the first line.
  if (!path.empty()) {
    const std::string desired = notes::pathFor(text, path);
    if (desired != path && notes::move(path, desired, text)) path = desired;
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

int NoteEditorActivity::headerHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight;
}

int NoteEditorActivity::textTop() const { return headerHeight() + MARGIN / 2; }

int NoteEditorActivity::toolbarTop() const { return keyboard.rect(renderer).y - TOOLBAR_HEIGHT; }

int NoteEditorActivity::textBottom() const {
  return keyboardShown.load() ? toolbarTop() - MARGIN / 2 : renderer.getScreenHeight() - MARGIN;
}

int NoteEditorActivity::lineHeight(const int line) const {
  const uint8_t font = line >= 0 && line < static_cast<int>(lineFonts.size()) ? lineFonts[line] : 0;
  return renderer.getLineHeight(fonts[font]);
}

int NoteEditorActivity::lineAtY(const int y) const {
  const int lines = static_cast<int>(lineStarts.size());
  int top = textTop();
  int line = topLine;
  while (line < lines && y >= top + lineHeight(line)) top += lineHeight(line++);
  return line;
}

int NoteEditorActivity::maxTopLine() const {
  const int space = textBottom() - textTop();
  int top = static_cast<int>(lineStarts.size());
  int used = 0;
  while (top > 0 && used + lineHeight(top - 1) <= space) used += lineHeight(--top);
  return std::min(top, std::max(0, static_cast<int>(lineStarts.size()) - 1));
}

int NoteEditorActivity::linesFrom(const int top) const {
  const int space = textBottom() - textTop();
  int line = top;
  int used = 0;
  while (line < static_cast<int>(lineStarts.size()) && used + lineHeight(line) <= space) used += lineHeight(line++);
  return std::max(1, line - top);
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
  if (count > 0 || touch.anyDown()) lastInputAt = now;
  const int width = renderer.getScreenWidth();
  for (uint8_t i = 0; i < count; ++i) {
    const TouchTracker::Press& p = presses[i];
    if (p.y < headerHeight()) {
      // The back arrow reaches loop() as Button::Back (HeaderBackTapTarget).
      if (p.x >= 2 * width / 3) {
        enqueue(Event{EventType::ToggleKeyboard, 0, p.x, p.y, p.downMs});
        update = true;
      }
      continue;
    }
    // The toolbar is checked before the keyboard, whose keys reach a little past their edges.
    if (keyboardShown.load() && p.y >= toolbarTop() && p.y < keyboard.rect(renderer).y) {
      const int format = std::clamp(p.x * FORMAT_COUNT / width, 0, FORMAT_COUNT - 1);
      enqueue(Event{EventType::Format, static_cast<int16_t>(format), p.x, p.y, p.downMs});
      update = true;
      continue;
    }
    int16_t value = 0;
    if (keyboardShown.load() && keyboard.keyAt(p.x, p.y, value)) {
      touch.setPressedValue(touch.pressSlot(i), value);
      if (value == fui::QWERTY_KEY_BACKSPACE) {
        repeatFor = p.downMs;
        nextRepeatAt = p.downMs + REPEAT_DELAY_MS;
      }
      if (TypingKeyboard::alternateOf(value) != 0) alternateFor = p.downMs;
      enqueue(Event{EventType::Key, value, p.x, p.y, p.downMs});
      update = true;
    } else if (p.y >= textTop() && p.y < textBottom() && dragSlot < 0) {
      dragSlot = static_cast<int8_t>(touch.pressSlot(i));
      dragDownMs = p.downMs;
      dragStartX = p.x;
      dragStartY = dragAppliedY = p.y;
      dragMoved = false;
    }
  }
  trackDrag(now, update);

  // Holding е long enough turns it into ё, like the iPhone's long-press menu.
  if (alternateFor != 0) {
    TouchTracker::Held held{};
    bool still = false;
    for (const int16_t base : {int16_t{0x0435}, int16_t{0x0415}}) {
      if (touch.heldValue(base, held) && held.downMs == alternateFor) {
        still = true;
        if (now - held.downMs >= ALTERNATE_DELAY_MS) {
          enqueue(Event{EventType::Alternate, base, held.x, held.y, now});
          alternateFor = 0;
          update = true;
        }
      }
    }
    if (!still) alternateFor = 0;
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

// A finger on the text drags it line by line; lifting it without a drag is a tap.
void NoteEditorActivity::trackDrag(const uint32_t now, bool& update) {
  if (dragSlot < 0) return;
  int16_t x = 0;
  int16_t y = 0;
  if (!touch.position(static_cast<uint8_t>(dragSlot), dragDownMs, x, y)) {
    if (!dragMoved) {
      enqueue(Event{EventType::TapText, 0, dragStartX, dragStartY, now});
      update = true;
    }
    dragSlot = -1;
    return;
  }
  if (!dragMoved && std::abs(y - dragStartY) < DRAG_SLOP && std::abs(x - dragStartX) < DRAG_SLOP) return;
  dragMoved = true;
  const int lineHeight = renderer.getLineHeight(fonts[0]);
  const int lines = (dragAppliedY - y) / lineHeight;  // finger up = text up = later lines
  if (lines == 0) return;
  dragAppliedY = static_cast<int16_t>(dragAppliedY - lines * lineHeight);
  enqueue(Event{EventType::ScrollBy, static_cast<int16_t>(lines), x, y, now});
  update = true;
}

// The main loop spins without pauses only while typing; between bursts it may
// idle and lower the CPU clock. Pending idle frames keep it awake until drawn.
bool NoteEditorActivity::skipLoopDelay() {
  return touch.anyDown() || millis() - lastInputAt < TYPING_ACTIVE_MS || queueTail.load() != queueHead.load() ||
         releaseFrame.load() || cleanupFrame.load() || pressedShownAt.load() != 0 || lastEditFrameAt.load() != 0;
}

// --- render task ---------------------------------------------------------------

int NoteEditorActivity::advance(const uint32_t cp, const uint8_t style, const uint8_t font) {
  uint8_t* cache = widthCache[font][style & note_md::STYLE_MASK];
  if (cp < CACHED_CPS && cache[cp] != 0) return cache[cp];
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
  const int w = std::max(
      1, renderer.getTextAdvanceX(fonts[font], buf, static_cast<EpdFontFamily::Style>(style & note_md::STYLE_MASK)));
  if (cp < CACHED_CPS) cache[cp] = static_cast<uint8_t>(std::min(w, 255));
  return w;
}

int NoteEditorActivity::boxSize() const { return renderer.getLineHeight(fonts[0]) * 11 / 20; }

// Width of the code point at offset i as displayed: markup is zero-width, list
// markers take their glyph's width.
int NoteEditorActivity::widthAt(const size_t i, const uint32_t cp, const uint8_t font) {
  const uint8_t a = attrs[i];
  if (a & note_md::HIDDEN) return 0;
  switch (note_md::glyphOf(a)) {
    case note_md::Glyph::Bullet:
      return advance(0x2022, 0, font);
    case note_md::Glyph::Box:
    case note_md::Glyph::BoxChecked:
      return boxSize() + BOX_GAP;
    case note_md::Glyph::Quote:
      return QUOTE_WIDTH;
    case note_md::Glyph::Rule:
      return 0;
    case note_md::Glyph::CodeEdge:
      return CODE_PAD;
    case note_md::Glyph::None:
      break;
  }
  return advance(cp, a, font);
}

// The paragraph with the cursor shows its markup while the keyboard is up.
void NoteEditorActivity::restyle() {
  size_t raw = std::string::npos;
  if (keyboardShown.load()) {
    const size_t nl = cursor == 0 ? std::string::npos : text.rfind('\n', cursor - 1);
    raw = nl == std::string::npos ? 0 : nl + 1;
  }
  note_md::style(text, raw, attrs);
  styleStale = false;
}

// Word wrap: break after the last space that fits; a word wider than the line breaks anywhere.
void NoteEditorActivity::wrap() {
  const int maxWidth = renderer.getScreenWidth() - 2 * MARGIN;
  // Headings use a larger font: each paragraph picks its slot once.
  const auto fontAt = [this](const size_t start) -> uint8_t {
    const int level = note_md::headingLevel(text, attrs, start);
    return level == 1 ? 2 : level == 2 ? 1 : 0;
  };
  uint8_t font = fontAt(0);
  lineStarts.clear();
  lineFonts.clear();
  lineStarts.push_back(0);
  lineFonts.push_back(font);
  size_t lineStart = 0;
  size_t lastBreak = std::string::npos;  // offset just after the last space on this line
  int width = 0;
  int widthSinceBreak = 0;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = 0;
    const size_t len = decode(text, i, cp);
    if (cp == '\n') {
      i += len;
      font = fontAt(i);
      lineStarts.push_back(static_cast<uint32_t>(i));
      lineFonts.push_back(font);
      lineStart = i;
      lastBreak = std::string::npos;
      width = widthSinceBreak = 0;
      continue;
    }
    const int w = widthAt(i, cp, font);
    if (width + w > maxWidth && i > lineStart && cp != ' ') {
      lineFonts.push_back(font);
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
  if (line < topLine) topLine = line;
  while (topLine < line && linesFrom(topLine) <= line - topLine) ++topLine;
}

void NoteEditorActivity::insertText(const char* utf8) {
  const size_t len = strlen(utf8);
  if (text.size() + len > notes::MAX_NOTE_BYTES) return;
  text.insert(cursor, utf8, len);
  cursor += len;
  dirty = true;
  styleStale = true;
}

void NoteEditorActivity::deleteBackward() {
  if (cursor == 0) return;
  size_t i = cursor - 1;
  while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i, cursor - i);
  cursor = i;
  dirty = true;
  styleStale = true;
}

// Replaces the е just typed with ё when the key is held.
void NoteEditorActivity::applyAlternate(const int16_t) {
  if (!TypingKeyboard::replaceWithAlternate(text, cursor)) return;
  dirty = true;
  styleStale = true;
}

void NoteEditorActivity::applyKey(const int16_t value, const uint32_t downMs, bool& layoutChanged) {
  char out[4] = {};
  const TypingKeyboard::Action action = keyboard.press(value, downMs, out);
  switch (action) {
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
      insertNewline();
      break;
    case TypingKeyboard::Action::LayoutChanged:
      layoutChanged = true;
      break;
    case TypingKeyboard::Action::None:
      break;
  }
  // Only edits move the sentence start; Shift and layer keys keep the reader's choice.
  if (action == TypingKeyboard::Action::Insert || action == TypingKeyboard::Action::Backspace ||
      action == TypingKeyboard::Action::Enter) {
    if (keyboard.autoCapitalize(text, cursor)) layoutChanged = true;
  }
}

// The marker that opens the paragraph with the cursor: a checkbox, a bullet or a
// heading. Each toolbar button toggles its own kind and replaces the others,
// like the format buttons in Apple Notes.
void NoteEditorActivity::applyFormat(const int16_t format) {
  const size_t nl = cursor == 0 ? std::string::npos : text.rfind('\n', cursor - 1);
  const size_t start = nl == std::string::npos ? 0 : nl + 1;
  size_t end = text.find('\n', start);
  if (end == std::string::npos) end = text.size();
  size_t at = start;
  while (at < end && text[at] == ' ') ++at;

  const auto isBulletChar = [](const char c) { return c == '-' || c == '*' || c == '+'; };
  const bool bullet = at + 1 < end && isBulletChar(text[at]) && text[at + 1] == ' ';
  const bool box = bullet && at + 4 < end && text[at + 2] == '[' && text[at + 4] == ']' &&
                   (text[at + 3] == ' ' || text[at + 3] == 'x' || text[at + 3] == 'X');
  int level = 0;
  while (at + level < end && text[at + level] == '#' && level < 6) ++level;
  if (at + level >= end || text[at + level] != ' ') level = 0;

  size_t oldLength = 0;
  if (box) {
    oldLength = at + 5 < end && text[at + 5] == ' ' ? 6 : 5;
  } else if (bullet) {
    oldLength = 2;
  } else if (level > 0) {
    oldLength = level + 1;
  }

  const char* marker = "";
  switch (format) {
    case FORMAT_CHECKBOX:
      marker = box ? "" : "- [ ] ";
      break;
    case FORMAT_LIST:
      marker = bullet && !box ? "" : "- ";
      break;
    case FORMAT_HEADING:
      marker = level == 0 ? "# " : level == 1 ? "## " : "";
      break;
    default:
      return;
  }
  const size_t newLength = strlen(marker);
  if (text.size() - oldLength + newLength > notes::MAX_NOTE_BYTES) return;
  text.replace(at, oldLength, marker);
  if (cursor >= at + oldLength) {
    cursor = cursor - oldLength + newLength;
  } else if (cursor > at) {
    cursor = at + newLength;
  }
  dirty = true;
  styleStale = true;
}

// Enter continues a list; on an empty item it ends the list instead.
void NoteEditorActivity::insertNewline() {
  const size_t nl = cursor == 0 ? std::string::npos : text.rfind('\n', cursor - 1);
  const size_t start = nl == std::string::npos ? 0 : nl + 1;
  size_t end = text.find('\n', cursor);
  if (end == std::string::npos) end = text.size();
  const note_md::ListPrefix prefix = note_md::listPrefix(text, start, end);
  if (prefix.length == 0 || cursor < start + prefix.length) {
    insertText("\n");
  } else if (prefix.empty && cursor == end) {
    text.erase(start, prefix.length);
    cursor = start;
    dirty = true;
    styleStale = true;
  } else {
    insertText(("\n" + prefix.next).c_str());
  }
}

// A tap on a checkbox toggles it instead of placing the cursor.
bool NoteEditorActivity::toggleCheckboxAt(const int x, const int y) {
  // Earlier events in this batch may have changed the text: lay it out again.
  if (styleStale) {
    restyle();
    wrap();
  }
  const int line = lineAtY(y);
  if (y < textTop() || line >= static_cast<int>(lineStarts.size())) return false;
  const size_t end = line + 1 < static_cast<int>(lineStarts.size()) ? lineStarts[line + 1] : text.size();
  int penX = MARGIN;
  for (size_t i = lineStarts[line]; i < end;) {
    uint32_t cp = 0;
    const size_t len = decode(text, i, cp);
    if (cp == '\n') break;
    const int w = widthAt(i, cp, lineFonts[line]);
    const note_md::Glyph glyph = note_md::glyphOf(attrs[i]);
    if ((glyph == note_md::Glyph::Box || glyph == note_md::Glyph::BoxChecked) && x >= penX - BOX_GAP && x < penX + w) {
      const size_t mark = note_md::checkboxMark(i);
      text[mark] = text[mark] == ' ' ? 'x' : ' ';
      dirty = true;
      styleStale = true;
      return true;
    }
    penX += w;
    i += len;
  }
  return false;
}

void NoteEditorActivity::placeCursor(const int x, const int y) {
  // Earlier events in this batch may have changed the text: lay it out again.
  if (styleStale) {
    restyle();
    wrap();
  }
  const int line = std::clamp(lineAtY(y), 0, static_cast<int>(lineStarts.size()) - 1);
  size_t i = lineStarts[line];
  const size_t end = line + 1 < static_cast<int>(lineStarts.size()) ? lineStarts[line + 1] : text.size();
  int penX = MARGIN;
  while (i < end) {
    uint32_t cp = 0;
    const size_t len = decode(text, i, cp);
    if (cp == '\n') break;
    const int w = widthAt(i, cp, lineFonts[line]);
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
    case EventType::Alternate:
      applyAlternate(event.value);
      break;
    case EventType::TapText:
      if (toggleCheckboxAt(event.x, event.y)) break;
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
      topLine = std::max(0, topLine - std::max(1, linesFrom(std::max(0, topLine - 1)) - 1));
      break;
    case EventType::ScrollDown:
      topLine = std::min(maxTopLine(), topLine + std::max(1, linesFrom(topLine) - 1));
      break;
    case EventType::ScrollBy:
      topLine = std::clamp(topLine + event.value, 0, maxTopLine());
      break;
    case EventType::Format:
      applyFormat(event.value);
      if (keyboard.autoCapitalize(text, cursor)) layoutChanged = true;
      keepCursorVisible();
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

// The system header: status band, back arrow and a keyboard toggle. No title:
// the list already says "Notes" and the note's first line names it.
void NoteEditorActivity::drawHeader() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeaderWithAction(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "",
                           keyboardShown.load() ? icon_keyboard_off_32 : icon_keyboard_32);
}

void NoteEditorActivity::drawToolbar() const {
  static const freeink::Icon* const ICONS[FORMAT_COUNT] = {&icon_fmt_checkbox_28, &icon_fmt_list_28,
                                                           &icon_fmt_heading_28};
  const int top = toolbarTop();
  const int width = renderer.getScreenWidth();
  renderer.drawLine(0, top, width - 1, top);
  for (int i = 0; i < FORMAT_COUNT; ++i) {
    const freeink::Icon& icon = *ICONS[i];
    const int cx = width * (2 * i + 1) / (2 * FORMAT_COUNT);
    renderer.drawIcon(icon.bits, cx - icon.w / 2, top + (TOOLBAR_HEIGHT - icon.h) / 2, icon.w);
  }
}

void NoteEditorActivity::drawGlyph(const uint8_t attr, const int x, const int y, const int lineHeight) {
  switch (note_md::glyphOf(attr)) {
    case note_md::Glyph::Bullet:
      renderer.drawText(fonts[0], x, y, "•");
      break;
    case note_md::Glyph::Box:
    case note_md::Glyph::BoxChecked: {
      const int size = boxSize();
      const int bx = x + 1;
      const int by = y + (lineHeight - size) / 2 + 1;
      renderer.drawRect(bx, by, size, size, 2, true);
      if (note_md::glyphOf(attr) == note_md::Glyph::BoxChecked) {
        renderer.drawLine(bx + size / 5, by + size / 2, bx + size * 2 / 5, by + size * 3 / 4, 2, true);
        renderer.drawLine(bx + size * 2 / 5, by + size * 3 / 4, bx + size * 4 / 5, by + size / 4, 2, true);
      }
      break;
    }
    case note_md::Glyph::Quote:
      renderer.fillRect(x + 2, y, 3, lineHeight);
      break;
    case note_md::Glyph::Rule:
      renderer.drawLine(MARGIN, y + lineHeight / 2, renderer.getScreenWidth() - MARGIN, y + lineHeight / 2, 2, true);
      break;
    case note_md::Glyph::CodeEdge:  // padding inside the code frame
    case note_md::Glyph::None:
      break;
  }
}

// Each line is drawn as runs of one style; markup is skipped, list markers
// become glyphs. Positions come from widthAt so the cursor matches the wrap.
void NoteEditorActivity::drawText() {
  const int lines = static_cast<int>(lineStarts.size());
  const int last = std::min(lines, topLine + linesFrom(topLine));
  // The cursor shows only while typing: without the keyboard the note is read.
  const int cursorLine = keyboardShown.load() ? lineOf(cursor) : -1;
  std::string run;
  int y = textTop();
  for (int l = topLine; l < last; ++l) {
    const size_t start = lineStarts[l];
    const size_t end = l + 1 < lines ? lineStarts[l + 1] : text.size();
    const uint8_t font = lineFonts[l];
    const int lineHeight = renderer.getLineHeight(fonts[font]);
    int x = MARGIN;
    int runX = x;
    uint8_t runAttr = 0;
    int cursorX = -1;
    int codeX = -1;  // left edge of the open code frame on this line
    const auto closeCode = [&]() {
      if (codeX < 0) return;
      renderer.drawRoundedRect(codeX, y + 2, x - codeX, lineHeight - 3, 1, 4, true);
      codeX = -1;
    };
    // The run measured as drawn (with kerning), so the caret sits on the glyph boundary.
    const auto caretX = [&]() {
      if (run.empty()) return x;
      return runX + renderer.getTextAdvanceX(fonts[font], run.c_str(),
                                             static_cast<EpdFontFamily::Style>(runAttr & note_md::STYLE_MASK));
    };
    const auto flush = [&]() {
      if (run.empty()) return;
      renderer.drawText(fonts[font], runX, y, run.c_str(), true,
                        static_cast<EpdFontFamily::Style>(runAttr & note_md::STYLE_MASK));
      if (runAttr & note_md::STRIKE) renderer.fillRect(runX, y + lineHeight * 11 / 20, x - runX, 2);
      run.clear();
    };
    for (size_t i = start; i < end;) {
      if (i == cursor) cursorX = caretX();
      uint32_t cp = 0;
      const size_t len = decode(text, i, cp);
      if (cp == '\n') break;
      const uint8_t a = attrs[i];
      const int w = widthAt(i, cp, font);
      if (a & note_md::HIDDEN) {
        i += len;
        continue;
      }
      if ((a & note_md::CODE) && codeX < 0) codeX = x;
      if (!(a & note_md::CODE) && codeX >= 0) closeCode();
      if (note_md::glyphOf(a) != note_md::Glyph::None) {
        flush();
        drawGlyph(a, x, y, lineHeight);
      } else {
        const uint8_t style = a & (note_md::STYLE_MASK | note_md::STRIKE);
        if (!run.empty() && style != runAttr) flush();
        if (run.empty()) {
          runX = x;
          runAttr = style;
        }
        run.append(text, i, len);
      }
      x += w;
      i += len;
    }
    if (l == cursorLine && cursorX < 0) cursorX = caretX();  // the cursor ends the line
    flush();
    closeCode();
    if (l == cursorLine) renderer.fillRect(cursorX - 1, y, 2, lineHeight);  // centered on the boundary
    y += lineHeight;
  }
}

void NoteEditorActivity::render(RenderLock&&) {
  bool layoutChanged = false;
  int keyEvents = 0;
  int events = 0;
  Event event{};
  while (popEvent(event)) {
    if (event.type == EventType::Key || event.type == EventType::Alternate || event.type == EventType::Format) {
      ++keyEvents;
    }
    ++events;
    handleEvent(event, layoutChanged);
  }

  const bool whole = fullRedraw.exchange(false);
  const bool release = keyEvents == 0 && releaseFrame.exchange(false) && pressedKey != INT16_MIN;
  const bool cleanup = keyEvents == 0 && cleanupFrame.exchange(false);
  if (release || cleanup) pressedKey = INT16_MIN;
  if (cleanup) saveNow();
  if (!whole && events == 0 && !release && !cleanup) return;

  restyle();
  wrap();
  if (keyEvents > 0) keepCursorVisible();
  // Never scroll past the end: hiding the keyboard pulls the text back down.
  topLine = std::clamp(topLine, 0, maxTopLine());

  renderer.clearScreen();
  drawHeader();
  drawText();
  if (keyboardShown.load()) {
    drawToolbar();
    keyboard.draw(renderer, pressedKey);
  }

  if (whole) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (cleanup) {
    fast_typing::CleanRefreshScope clean;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  const uint32_t done = millis();

  // Any fast frame (typing, scrolling) leaves residue for the idle cleanup.
  if (events > 0 || release) lastEditFrameAt = done;
  if (cleanup || whole) lastEditFrameAt = 0;
  pressedShownAt = pressedKey != INT16_MIN ? done : 0;
  if (releaseFrame.load()) requestUpdate();
}
