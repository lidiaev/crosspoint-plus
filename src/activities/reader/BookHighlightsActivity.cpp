#include "BookHighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr size_t MAX_COMMENT_BYTES = 2000;
constexpr size_t SUBTITLE_BYTES = 120;
}  // namespace

BookHighlightsActivity::BookHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string bookPath, std::string bookTitle, const bool opensPlace)
    : UiListActivity("BookHighlights", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      bookPath(std::move(bookPath)),
      bookTitle(std::move(bookTitle)),
      opensPlace(opensPlace) {}

void BookHighlightsActivity::onEnter() {
  UiListActivity::onEnter();
  if (!highlights::load(bookPath, entries)) LOG_ERR("HLT", "Failed to load highlights");
  rebuildRows();
  requestUpdate();
}

void BookHighlightsActivity::rebuildRows() {
  subtitles.clear();
  rows.clear();
  subtitles.reserve(entries.size());
  rows.reserve(entries.size());
  for (const highlights::Highlight& entry : entries) {
    std::string subtitle = entry.spine >= 0 ? std::to_string(entry.percent) + "%" : "";
    if (!entry.comment.empty()) {
      if (!subtitle.empty()) subtitle += " - ";
      std::string start = entry.comment.substr(0, SUBTITLE_BYTES);
      while (!start.empty() && (static_cast<uint8_t>(start.back()) & 0xC0) == 0x80) start.pop_back();
      if (!start.empty() && static_cast<uint8_t>(start.back()) >= 0xC0) start.pop_back();
      std::replace(start.begin(), start.end(), '\n', ' ');
      subtitle += start;
    }
    subtitles.push_back(std::move(subtitle));
    fui::ListItem item;
    item.label = entry.quote.c_str();
    item.subtitle = subtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(rows.size());
    rows.push_back(item);
  }
}

void BookHighlightsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS));
    return;
  }
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<int16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void BookHighlightsActivity::render(RenderLock&& lock) {
  if (popup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void BookHighlightsActivity::activateIndex(const int index) {
  if (popup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  const highlights::Highlight& entry = entries[index];
  if (entry.spine < 0 || !opensPlace) {  // written by hand, or the book is not open
    showActions(index);
    return;
  }
  ProgressChangeResult result{};
  result.spineIndex = entry.spine;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = entry.start;
  result.percentage = static_cast<float>(entry.percent) / 100.0f;
  result.hasSavedProgress = true;
  setResult(std::move(result));
  finish();
}

void BookHighlightsActivity::onRowLongPress(const int index) {
  if (popup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showActions(index);
}

void BookHighlightsActivity::showActions(const int index) {
  const StrId options[] = {StrId::STR_COMMENT, StrId::STR_DELETE};
  popup.showActions(entries[index].quote.c_str(), options, 2, [this, index](const int choice) {
    if (choice == 0) {
      editComment(index);
    } else {
      deleteEntry(index);
    }
  });
  requestUpdate();
}

void BookHighlightsActivity::editComment(const int index) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_COMMENT),
                                                           entries[index].comment, MAX_COMMENT_BYTES, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
    if (!result.isCancelled && index < listCount()) {
      entries[index].comment = std::get<KeyboardResult>(result.data).text;
      if (!highlights::save(bookPath, bookTitle, entries)) LOG_ERR("HLT", "Failed to save highlights");
      rebuildRows();
    }
    requestUpdate();
  });
}

void BookHighlightsActivity::deleteEntry(const int index) {
  entries.erase(entries.begin() + index);
  if (!highlights::save(bookPath, bookTitle, entries)) LOG_ERR("HLT", "Failed to save highlights");
  rebuildRows();
  nav.selected = std::clamp(nav.selected.load(), 0, std::max(0, listCount() - 1));
  requestUpdate();
}

bool BookHighlightsActivity::handleCustomInput() { return popup.handleInput(mappedInput, [this] { requestUpdate(); }); }

bool BookHighlightsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  return false;
}
