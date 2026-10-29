#include "NotesListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "NoteEditorActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

void NotesListActivity::onEnter() {
  notesList = notes::list();
  rows.clear();
  rows.reserve(notesList.size() + 1);
  newNoteLabel = std::string("+  ") + tr(STR_NEW_NOTE);
  fui::ListItem add;
  add.label = newNoteLabel.c_str();
  add.actionValue = 0;
  rows.push_back(add);
  for (const auto& note : notesList) {
    fui::ListItem item;
    item.label = note.title.c_str();
    item.icon = listIconFor(UIIcon::Text);
    item.actionValue = static_cast<int16_t>(rows.size());
    rows.push_back(item);
  }
  UiListActivity::onEnter();
}

const char* NotesListActivity::headerTitle() const { return tr(STR_NOTES); }

void NotesListActivity::onBackButton() { onGoHome(HomeMenuItem::NOTES); }

void NotesListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void NotesListActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  std::string path = index == 0 ? std::string() : notesList[index - 1].path;
  activityManager.replaceActivity(std::make_unique<NoteEditorActivity>(renderer, mappedInput, std::move(path)));
}
