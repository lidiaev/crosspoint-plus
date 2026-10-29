#pragma once

#include <string>
#include <vector>

#include "NotesStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Home > Notes: "New note" followed by the notes in /notes, newest first.
// The header searches titles and text, like the Library's. Holding a note
// opens its menu: open, pin or unpin, delete.
class NotesListActivity final : public UiListActivity {
 public:
  NotesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("NotesList", renderer, mappedInput, true) {}

  void onEnter() override;
  void render(RenderLock&& lock) override;

 protected:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void drawChrome() override {}
  void drawFooter() override {}
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;

 private:
  static constexpr freeink::ui::ActionId ACTION_SEARCH = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_BACK = ACTION_USER + 1;

  std::vector<notes::NoteInfo> notesList;
  std::vector<int> shown;  // notesList indices matching the query, in order
  std::vector<freeink::ui::ListItem> rows;
  std::string query;
  std::string searchTitle;  // the query, quoted for the header

  OptionPopup optionPopup;

  int noteAt(int index) const;  // notesList index of a row, -1 for "New note"
  void reload();
  void promptDelete(const notes::NoteInfo& note);
  void applyFilter();
  void rebuildRows();
  void openSearch();
  void buildHeader(UiScreen& screen);
  static void searchTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void backTrampoline(const freeink::ui::ActionEvent& event, void* user);
};
