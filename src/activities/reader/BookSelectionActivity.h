#pragma once

#include <Epub/Page.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "BookHighlights.h"
#include "activities/Activity.h"
#include "components/SelectionCallout.h"
#include "components/typing/TouchTracker.h"

// Text selection over the current book page, in whole words. A held finger
// selects a word and extends the selection as it moves; handles move either end
// within the page. The callout marks, comments, looks up or copies. Opened on an
// existing highlight, it shows the comment and edits or deletes it instead.
class BookSelectionActivity final : public Activity {
 public:
  struct Book {
    std::string path;
    std::string title;
    int spine = 0;
    uint8_t percent = 0;
  };

  // `entries` is the reader's list and outlives this screen. `existing` is an
  // index into it, or -1 to select the word at (x, y).
  BookSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        int marginLeft, int marginTop, Book book, std::vector<highlights::Highlight>& entries, int x,
                        int y, int existing)
      : Activity("BookSelection", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        book(std::move(book)),
        entries(entries),
        startX(x),
        startY(y),
        existing(existing) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override;

 private:
  struct Word {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    uint32_t offset;
    const char* text;  // in the owned Page's arena
  };
  enum class Action : uint8_t { Mark, Comment, Dictionary, Copy, Delete };
  enum class Drag : uint8_t { None, Extend, StartHandle, EndHandle };
  static constexpr int MAX_ACTIONS = 4;

  void extractWords();
  int nearestWord(int x, int y) const;
  int actions(Action out[MAX_ACTIONS]) const;
  void run(Action action);
  highlights::Highlight& entryForSelection();
  void growOverHighlights();
  void saveEntries();
  std::string selectedText() const;
  void drawBase();
  void drawSelection();
  void drawComment(bool atTop);

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  Book book;
  std::vector<highlights::Highlight>& entries;
  const int startX;
  const int startY;
  int existing;
  int fontId = 0;
  int lineHeight = 0;
  std::vector<Word> words;
  std::vector<int16_t> wordEntry;  // the highlight already drawn on each word, -1 for none

  // Input task writes the range, the render task draws it.
  std::atomic<int> firstWord{0};
  std::atomic<int> lastWord{0};
  int anchorWord = 0;
  TouchTracker touch;
  uint32_t enteredAt = 0;
  Drag drag = Drag::None;
  int8_t dragSlot = -1;
  uint32_t dragDownMs = 0;
  bool awaitingLift = false;  // a tap was decided: act on lift so the reader does not see the release
  int pendingItem = -1;       // callout item to run on lift; -1 closes the screen

  // Drawn by the render task; a torn read misroutes at most one tap.
  SelectionCallout callout;
  int startHandleX = 0;
  int startHandleY = 0;
  int endHandleX = 0;
  int endHandleY = 0;

  // The page as drawn, restored under each selection change instead of reloading its glyphs.
  std::unique_ptr<uint8_t[]> base;
  size_t baseSize = 0;
  bool baseReady = false;
};
