#include "BookSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/icons/selectionIcons.h"
#include "components/typing/TextSelection.h"
#include "fontIds.h"

namespace {
constexpr int HANDLE_REACH = 36;
constexpr int INITIAL_REACH = 48;           // the finger that opened the screen keeps extending
constexpr uint32_t INITIAL_MS = 1500;       // while it can still be that finger
constexpr size_t MAX_COMMENT_BYTES = 2000;  // the field, not the file
constexpr int COMMENT_LINES = 4;
constexpr int COMMENT_MARGIN = 14;

size_t codePoints(const char* text) {
  size_t count = 0;
  for (; *text != '\0'; ++text) {
    if ((static_cast<uint8_t>(*text) & 0xC0) != 0x80) ++count;
  }
  return count;
}
}  // namespace

void BookSelectionActivity::onEnter() {
  Activity::onEnter();
  enteredAt = millis();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  baseSize = static_cast<size_t>(renderer.getDisplayWidthBytes()) * renderer.getDisplayHeight();
  base = makeUniqueNoThrow<uint8_t[]>(baseSize);
  extractWords();
  if (words.empty()) {
    requestUpdate();
    return;
  }
  int first = 0;
  int last = 0;
  if (existing >= 0) {
    const highlights::Highlight& entry = entries[existing];
    first = static_cast<int>(words.size()) - 1;
    for (int i = 0; i < static_cast<int>(words.size()); ++i) {
      if (words[i].offset < entry.start || words[i].offset >= entry.end) continue;
      first = std::min(first, i);
      last = std::max(last, i);
    }
    last = std::max(first, last);
  } else {
    first = last = nearestWord(startX, startY);
  }
  firstWord = first;
  lastWord = last;
  anchorWord = first;
  requestUpdate();
}

void BookSelectionActivity::extractWords() {
  words.clear();
  words.reserve(256);
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0x01;
  uint16_t row = 0;
  const int ascender = renderer.getFontAscenderSize(fontId);
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid() || block->wordCount() == 0) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      pageText.append(block->wordText(i));
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
    }
    ++row;
  }
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);

  row = 0;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid() || block->wordCount() == 0) continue;
    const int8_t tracking = block->getBlockStyle().characterSpacing;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      Word word{};
      word.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      word.y = static_cast<int16_t>(line->yPos + marginTop + block->getRubyShift(ascender));
      word.width = static_cast<int16_t>(
          renderer.getTextAdvanceX(fontId, block->wordText(i), block->wordStyle(i), tracking));
      word.row = row;
      word.offset = block->wordVisibleOffset(i);
      word.text = block->wordText(i);
      words.push_back(word);
    }
    ++row;
  }
}

// The nearest word on the nearest row: a finger on a margin or between lines still lands somewhere.
int BookSelectionActivity::nearestWord(const int x, const int y) const {
  int best = 0;
  int bestRowDistance = INT_MAX;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); ++i) {
    const Word& word = words[i];
    const int rowDistance = std::abs(word.y + lineHeight / 2 - y);
    const int distance = x < word.x ? word.x - x : x >= word.x + word.width ? x - word.x - word.width : 0;
    if (rowDistance < bestRowDistance || (rowDistance == bestRowDistance && distance < bestDistance)) {
      best = i;
      bestRowDistance = rowDistance;
      bestDistance = distance;
    }
  }
  return best;
}

int BookSelectionActivity::actions(Action out[MAX_ACTIONS]) const {
  int n = 0;
  const bool oneWord = firstWord.load() == lastWord.load();
  const bool lookup = oneWord && SETTINGS.dictionaryName[0] != '\0';
  if (existing >= 0) {
    out[n++] = Action::Comment;
    out[n++] = Action::Copy;
    out[n++] = Action::Delete;
  } else {
    out[n++] = Action::Mark;
    out[n++] = Action::Comment;
    out[n++] = Action::Copy;
  }
  if (lookup) out[n++] = Action::Dictionary;
  return n;
}

// The selection as text: words that touch in the book stay together, and a
// line-end hyphen the layout added goes away.
std::string BookSelectionActivity::selectedText() const {
  std::string out;
  const int last = lastWord.load();
  for (int i = firstWord.load(); i <= last; ++i) {
    const char* text = words[i].text;
    size_t length = strlen(text);
    bool joined = false;
    if (i < last) {
      const uint32_t end = words[i].offset + static_cast<uint32_t>(codePoints(text));
      if (end == words[i + 1].offset) {
        joined = true;
      } else if (end == words[i + 1].offset + 1 && length > 0 && text[length - 1] == '-') {
        --length;
        joined = true;
      }
    }
    out.append(text, length);
    if (i < last && !joined) out.push_back(' ');
  }
  return out;
}

highlights::Highlight& BookSelectionActivity::entryForSelection() {
  if (existing >= 0) return entries[existing];
  highlights::Highlight entry;
  entry.spine = book.spine;
  entry.start = words[firstWord.load()].offset;
  const Word& last = words[lastWord.load()];
  entry.end = last.offset + static_cast<uint32_t>(codePoints(last.text));
  entry.percent = book.percent;
  entry.quote = selectedText();
  entries.push_back(std::move(entry));
  existing = static_cast<int>(entries.size()) - 1;
  return entries[existing];
}

void BookSelectionActivity::saveEntries() {
  if (!highlights::save(book.path, book.title, entries)) LOG_ERR("HLT", "Failed to save highlights");
}

void BookSelectionActivity::run(const Action action) {
  switch (action) {
    case Action::Mark:
      entryForSelection();
      saveEntries();
      finish();
      break;
    case Action::Comment: {
      const std::string current = entryForSelection().comment;
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_COMMENT), current,
                                                               MAX_COMMENT_BYTES, InputType::Text);
      if (!keyboard) {
        saveEntries();
        finish();
        break;
      }
      startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
        if (!result.isCancelled) entries[existing].comment = std::get<KeyboardResult>(result.data).text;
        saveEntries();
        finish();
      });
      break;
    }
    case Action::Dictionary: {
      const Word& word = words[firstWord.load()];
      setResult(LookupResult{word.x + word.width / 2, word.y + lineHeight / 2});
      finish();
      break;
    }
    case Action::Copy:
      clipboard::set(selectedText());
      finish();
      break;
    case Action::Delete:
      entries.erase(entries.begin() + existing);
      saveEntries();
      finish();
      break;
  }
}

void BookSelectionActivity::loop() {
  if (words.empty() || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const uint32_t now = millis();
  TouchTracker::Press presses[4];
  const uint8_t count = touch.poll(renderer, now, presses, 4);

  if (awaitingLift) {
    if (touch.anyDown()) return;
    awaitingLift = false;
    Action list[MAX_ACTIONS];
    const int n = actions(list);
    if (pendingItem >= 0 && pendingItem < n) {
      run(list[pendingItem]);
    } else if (pendingItem < 0) {
      finish();
    }
    return;
  }

  for (uint8_t i = 0; i < count; ++i) {
    const TouchTracker::Press& p = presses[i];
    if (drag != Drag::None) continue;
    const int item = callout.itemAt(p.x, p.y);
    if (item >= 0) {
      awaitingLift = true;
      pendingItem = item;
      return;
    }
    const auto near = [&p](const int hx, const int hy) {
      return std::abs(p.x - hx) < HANDLE_REACH && std::abs(p.y - hy) < HANDLE_REACH * 3 / 2;
    };
    if (existing < 0 && near(endHandleX, endHandleY)) {
      drag = Drag::EndHandle;
    } else if (existing < 0 && near(startHandleX, startHandleY)) {
      drag = Drag::StartHandle;
    } else if (existing < 0 && now - enteredAt < INITIAL_MS && std::abs(p.x - startX) < INITIAL_REACH &&
               std::abs(p.y - startY) < INITIAL_REACH) {
      drag = Drag::Extend;
    } else {
      awaitingLift = true;
      pendingItem = -1;
      return;
    }
    dragSlot = static_cast<int8_t>(touch.pressSlot(i));
    dragDownMs = p.downMs;
  }

  if (drag == Drag::None) return;
  int16_t x = 0;
  int16_t y = 0;
  if (!touch.position(static_cast<uint8_t>(dragSlot), dragDownMs, x, y)) {
    drag = Drag::None;
    return;
  }
  const int word = nearestWord(x, y);
  int first = firstWord.load();
  int last = lastWord.load();
  switch (drag) {
    case Drag::Extend:
      first = std::min(anchorWord, word);
      last = std::max(anchorWord, word);
      break;
    case Drag::StartHandle:
      first = std::min(word, last);
      break;
    case Drag::EndHandle:
      last = std::max(word, first);
      break;
    case Drag::None:
      break;
  }
  if (first != firstWord.load() || last != lastWord.load()) {
    firstWord = first;
    lastWord = last;
    requestUpdate();
  }
}

bool BookSelectionActivity::skipLoopDelay() { return touch.anyDown() || millis() - enteredAt < INITIAL_MS; }

void BookSelectionActivity::drawBase() {
  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);

  std::vector<highlights::Mark> marks;
  highlights::collectMarks(renderer, *page, fontId, marginLeft, marginTop, book.spine, entries, marks);
  // The highlight being opened is drawn as the selection.
  marks.erase(std::remove_if(marks.begin(), marks.end(),
                             [this](const highlights::Mark& mark) { return mark.entry == existing; }),
              marks.end());
  highlights::drawMarks(renderer, marks, entries);
  baseReady = base && renderer.readFramebufferRegion(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                                     base.get(), baseSize) > 0;
}

void BookSelectionActivity::drawSelection() {
  const bool invert = SETTINGS.selectionStyle == CrossPointSettings::SELECTION_INVERT;
  const int first = firstWord.load();
  const int last = lastWord.load();
  for (int i = first; i <= last;) {
    int runEnd = i;
    while (runEnd < last && words[runEnd + 1].row == words[i].row) ++runEnd;
    const int x = words[i].x;
    const int width = words[runEnd].x + words[runEnd].width - x;
    if (invert) {
      renderer.invertRect(x, words[i].y, width, lineHeight);
    } else {
      renderer.fillRect(x, words[i].y + lineHeight - 2, width, 2);
    }
    i = runEnd + 1;
  }
  const Word& start = words[first];
  const Word& end = words[last];
  startHandleX = start.x;
  startHandleY = start.y + lineHeight / 2;
  endHandleX = end.x + end.width;
  endHandleY = end.y + lineHeight / 2;
  if (existing >= 0) return;
  SelectionCallout::drawHandle(renderer, startHandleX, start.y, lineHeight, true);
  SelectionCallout::drawHandle(renderer, endHandleX, end.y, lineHeight, false);
}

// The comment on a band at the top or bottom of the screen, away from the selection.
void BookSelectionActivity::drawComment(const bool atTop) {
  const std::string& comment = entries[existing].comment;
  const int width = renderer.getScreenWidth() - 2 * COMMENT_MARGIN;
  const int rowHeight = renderer.getLineHeight(UI_12_FONT_ID);
  std::vector<std::string> rows;
  std::string row;
  size_t at = 0;
  while (at < comment.size() && static_cast<int>(rows.size()) < COMMENT_LINES) {
    size_t next = comment.find_first_of(" \n", at);
    if (next == std::string::npos) next = comment.size();
    std::string candidate = row;
    if (!candidate.empty()) candidate.push_back(' ');
    candidate.append(comment, at, next - at);
    if (renderer.getTextWidth(UI_12_FONT_ID, candidate.c_str()) > width && !row.empty()) {
      rows.push_back(row);
      row.clear();
      continue;  // the word opens the next row
    }
    row = candidate;
    at = next + 1;
    if (next < comment.size() && comment[next] == '\n') {
      rows.push_back(row);
      row.clear();
    }
  }
  if (!row.empty() && static_cast<int>(rows.size()) < COMMENT_LINES) rows.push_back(row);
  if (at < comment.size() && !rows.empty()) rows.back() += "…";

  const int bandHeight = static_cast<int>(rows.size()) * rowHeight + COMMENT_MARGIN;
  const int top = atTop ? 0 : renderer.getScreenHeight() - bandHeight;
  renderer.fillRect(0, top, renderer.getScreenWidth(), bandHeight, false);
  renderer.fillRect(0, atTop ? top + bandHeight - 2 : top, renderer.getScreenWidth(), 2);
  int y = top + COMMENT_MARGIN / 2;
  for (const std::string& text : rows) {
    renderer.drawText(UI_12_FONT_ID, COMMENT_MARGIN, y, text.c_str());
    y += rowHeight;
  }
}

void BookSelectionActivity::render(RenderLock&&) {
  if (words.empty()) return;
  if (baseReady) {
    renderer.writeFramebufferRegion(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), base.get());
  } else {
    drawBase();
  }
  drawSelection();

  const Word& start = words[firstWord.load()];
  const Word& end = words[lastWord.load()];
  const int height = renderer.getScreenHeight();
  const bool commentShown = existing >= 0 && !entries[existing].comment.empty();
  const bool commentAtTop = end.y > height / 2;
  if (commentShown) drawComment(commentAtTop);

  Action list[MAX_ACTIONS];
  const int n = actions(list);
  const freeink::Icon* icons[MAX_ACTIONS] = {};
  for (int i = 0; i < n; ++i) {
    switch (list[i]) {
      case Action::Mark:
        icons[i] = &icon_sel_highlight_24;
        break;
      case Action::Comment:
        icons[i] = &icon_sel_comment_24;
        break;
      case Action::Dictionary:
        icons[i] = &icon_sel_dictionary_24;
        break;
      case Action::Copy:
        icons[i] = &icon_sel_copy_24;
        break;
      case Action::Delete:
        icons[i] = &icon_sel_delete_24;
        break;
    }
  }
  callout.place(n, start.x, start.y, end.x + end.width, end.y + lineHeight, 0, height, renderer.getScreenWidth());
  callout.draw(renderer, icons);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
