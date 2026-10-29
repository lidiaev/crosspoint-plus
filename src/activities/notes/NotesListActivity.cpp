#include "NotesListActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "NoteEditorActivity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"
#include "components/icons/notesIcons.h"
#include "components/icons/search32.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void NotesListActivity::onEnter() {
  notesList = notes::list();
  applyFilter();
  UiListActivity::onEnter();
  app.on(ACTION_SEARCH, &NotesListActivity::searchTrampoline, this);
  app.on(ACTION_BACK, &NotesListActivity::backTrampoline, this);
  app.on(ACTION_NEW, &NotesListActivity::newNoteTrampoline, this);
}

// Every query word must prefix a word of the title or the text. Notes are
// small, so a search reads each file once; nothing is indexed.
void NotesListActivity::applyFilter() {
  shown.clear();
  shown.reserve(notesList.size());
  searchTitle = query.empty() ? std::string() : "“" + query + "”";
  const std::string needle = library::fold(query);
  std::string text;
  for (int i = 0; i < static_cast<int>(notesList.size()); ++i) {
    if (!needle.empty()) {
      const auto& note = notesList[i];
      if (!library::matchesQuery(library::fold(note.title), needle) &&
          !(notes::load(note.path, text) && library::matchesQuery(library::fold(text), needle))) {
        continue;
      }
    }
    shown.push_back(i);
  }
  rebuildRows();
}

void NotesListActivity::rebuildRows() {
  rows.clear();
  rows.reserve(shown.size());
  for (const int i : shown) {
    const auto& note = notesList[i];
    fui::ListItem item;
    item.label = note.title.c_str();
    if (!note.preview.empty()) item.subtitle = note.preview.c_str();
    item.icon = note.pinned ? fui::bitmapFromIcon(icon_pinned_32) : listIconFor(UIIcon::Text, 32);
    item.actionValue = static_cast<int16_t>(rows.size());
    rows.push_back(item);
  }
}

const char* NotesListActivity::headerTitle() const { return searchTitle.empty() ? tr(STR_NOTES) : searchTitle.c_str(); }

// Back clears an active search first, then leaves for home.
void NotesListActivity::onBackButton() {
  if (!query.empty()) {
    RenderLock lock;
    query.clear();
    applyFilter();
    moveSelectionTo(0);
    requestUpdate();
    return;
  }
  onGoHome(HomeMenuItem::NOTES);
}

void NotesListActivity::openSearch() {
  app.clearTapFlash();
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), query, 48, InputType::Search);
  if (!keyboard) {
    LOG_ERR("NOTES", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    RenderLock lock;
    query = std::get<KeyboardResult>(result.data).text;
    applyFilter();
    moveSelectionTo(0);
    requestUpdate();
  });
}

void NotesListActivity::searchTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<NotesListActivity*>(user)->openSearch();
}

void NotesListActivity::backTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<NotesListActivity*>(user)->onBackButton();
}

// The Library's header: status band, back arrow and a search button.
void NotesListActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = headerTitle();
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  GUI.applyHeaderStatus(renderer, header);
  header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
  header.leadingAction = ACTION_BACK;
  header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
  header.trailingAction = ACTION_SEARCH;
  header.trailingAdjacentIcon = fui::bitmapFromIcon(icon_note_new_32);
  header.trailingAdjacentAction = ACTION_NEW;
  const auto frameRect = screen.frame().screen();
  fui::header(screen.frame(),
              fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding), frameRect.width,
                        static_cast<int16_t>(metrics.headerHeight)},
              header);
}

void NotesListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  buildHeader(screen);
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // The position readout owns the line above the hints; rows must not overlap it.
  const int readoutReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + readoutReserved),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rows.empty()) {
    screen.centeredText(query.empty() ? tr(STR_NO_NOTES) : tr(STR_NO_NOTES_FOUND));
    return;
  }

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

int NotesListActivity::noteAt(const int index) const {
  return index < 0 || index >= static_cast<int>(shown.size()) ? -1 : shown[index];
}

void NotesListActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const int note = noteAt(index);
  if (note < 0) return;
  activityManager.replaceActivity(std::make_unique<NoteEditorActivity>(renderer, mappedInput, notesList[note].path));
}

void NotesListActivity::newNoteTrampoline(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<NotesListActivity*>(user);
  self->app.clearTapFlash();
  activityManager.replaceActivity(
      std::make_unique<NoteEditorActivity>(self->renderer, self->mappedInput, std::string()));
}

// Re-reads /notes after a change; the caller holds the render lock.
void NotesListActivity::reload() {
  notesList = notes::list();
  applyFilter();
  moveSelectionTo(0);
}

void NotesListActivity::onRowLongPress(const int index) {
  const int i = noteAt(index);
  if (i < 0) return;
  const notes::NoteInfo note = notesList[i];
  const char* options[] = {tr(STR_OPEN), note.pinned ? tr(STR_UNPIN_NOTE) : tr(STR_PIN_NOTE), tr(STR_DELETE)};
  app.clearTapFlash();
  optionPopup.showActions(note.title.c_str(), options, 3, [this, index, note](const int choice) {
    switch (choice) {
      case 0:
        activateIndex(index);
        break;
      case 1: {
        RenderLock lock;
        if (!notes::setPinned(note.path, !note.pinned)) LOG_ERR("NOTES", "Cannot pin %s", note.path.c_str());
        reload();
        break;
      }
      case 2:
        promptDelete(note);
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void NotesListActivity::promptDelete(const notes::NoteInfo& note) {
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "), note.title);
  if (!confirmation) {
    LOG_ERR("NOTES", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, path = note.path](const ActivityResult& result) {
    if (result.isCancelled) return;
    RenderLock lock;
    if (!notes::remove(path)) LOG_ERR("NOTES", "Cannot delete %s", path.c_str());
    reload();
  });
}

bool NotesListActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

// The option popup owns the whole frame while it is up.
void NotesListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

// "3/12" at the bottom right, as in the Library: the selected note out of all shown.
void NotesListActivity::drawFooter() {
  const int count = listCount();
  if (count <= 0) return;
  char buf[48];
  snprintf(buf, sizeof(buf), tr(STR_NOTES_POSITION), std::clamp(nav.selected.load(), 0, count - 1) + 1, count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = renderer.getScreenWidth() - renderer.getTextWidth(SMALL_FONT_ID, buf) - metrics.contentSidePadding;
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x, y, buf, true);
}
