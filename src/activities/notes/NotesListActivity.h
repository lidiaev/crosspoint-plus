#pragma once

#include <string>
#include <vector>

#include "NotesStore.h"
#include "activities/UiListActivity.h"

// Home > Notes: "New note" followed by the notes in /notes, newest first.
class NotesListActivity final : public UiListActivity {
 public:
  NotesListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("NotesList", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void drawFooter() override {}

 private:
  std::vector<notes::NoteInfo> notesList;
  std::vector<freeink::ui::ListItem> rows;
  std::string newNoteLabel;
};
