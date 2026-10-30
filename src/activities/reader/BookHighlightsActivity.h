#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "BookHighlights.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// A book's highlights in book order: the quote, the start of the comment and the
// percentage. A tap goes to the place in the book when the reader opened this
// screen; holding offers Comment and Delete.
class BookHighlightsActivity final : public UiListActivity {
 public:
  BookHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                         std::string bookTitle, bool opensPlace = true);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(entries.size()); }
  const char* headerTitle() const override { return tr(STR_HIGHLIGHTS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  void rebuildRows();
  void showActions(int index);
  void editComment(int index);
  void deleteEntry(int index);

  std::string bookPath;
  std::string bookTitle;
  const bool opensPlace;
  std::vector<highlights::Highlight> entries;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rows;
  OptionPopup popup;
};
