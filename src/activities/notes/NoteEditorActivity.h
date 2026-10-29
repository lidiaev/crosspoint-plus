#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "NoteMarkdown.h"
#include "activities/Activity.h"
#include "components/typing/TouchTracker.h"
#include "components/typing/TypingKeyboard.h"

// Markdown editor for one note, rendered in place (see NoteMarkdown.h). Keys commit on touch-down and reach the panel
// through the fast typing display mode (short LUT, changed regions only); an
// idle repaint with the factory waveform clears residue and autosaves.
class NoteEditorActivity final : public Activity {
 public:
  // An empty path starts a new note.
  NoteEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
      : Activity("NoteEditor", renderer, mappedInput), path(std::move(path)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override;

 private:
  enum class EventType : uint8_t { Key, Alternate, TapText, ToggleKeyboard, ScrollUp, ScrollDown, ScrollBy };
  struct Event {
    EventType type;
    int16_t value;
    int16_t x;
    int16_t y;
    uint32_t downMs;
  };
  static constexpr uint8_t QUEUE_SIZE = 32;

  // Input task (loop) produces events, the render task consumes them.
  Event queue[QUEUE_SIZE] = {};
  std::atomic<uint8_t> queueHead{0};
  std::atomic<uint8_t> queueTail{0};
  std::atomic<bool> fullRedraw{true};
  std::atomic<bool> cleanupFrame{false};
  std::atomic<bool> releaseFrame{false};
  std::atomic<bool> exitRequested{false};
  std::atomic<uint32_t> lastEditFrameAt{0};  // 0 = idle cleanup done
  std::atomic<uint32_t> pressedShownAt{0};   // 0 = no key shown inverted
  std::atomic<bool> keyboardShown{true};
  TouchTracker touch;
  uint32_t lastInputAt = 0;  // last touch or key, for skipLoopDelay
  uint32_t liftedAt = 0;
  bool wasDown = false;
  uint32_t nextRepeatAt = 0;
  uint32_t repeatFor = 0;     // downMs of the Backspace press being repeated
  uint32_t alternateFor = 0;  // downMs of the е press that may turn into ё
  // A finger that landed on the text: a tap places the cursor on lift, a drag scrolls.
  int8_t dragSlot = -1;
  uint32_t dragDownMs = 0;
  int16_t dragStartX = 0;
  int16_t dragStartY = 0;
  int16_t dragAppliedY = 0;  // finger y the scroll position already follows
  bool dragMoved = false;

  // Render task state.
  std::string path;
  std::string text;
  size_t cursor = 0;  // byte offset, always on a code point boundary
  bool dirty = false;
  TypingKeyboard keyboard;
  int16_t pressedKey = INT16_MIN;
  std::vector<uint32_t> lineStarts;  // byte offset of each wrapped line
  std::vector<uint8_t> lineFonts;    // font slot of each wrapped line (headings are larger)
  std::vector<uint8_t> attrs;        // note_md attributes, one per byte of text
  bool styleStale = true;            // text changed since attrs were computed
  int topLine = 0;
  static constexpr uint32_t CACHED_CPS = 0x460;
  static constexpr int FONT_SLOTS = 3;  // body, heading 2, heading 1
  // Advance per font slot, style and code point, 0 = unknown.
  uint8_t widthCache[FONT_SLOTS][4][CACHED_CPS] = {};

  void enqueue(const Event& event);
  bool popEvent(Event& event);
  void handleEvent(const Event& event, bool& layoutChanged);
  void applyKey(int16_t value, uint32_t downMs, bool& layoutChanged);
  void insertText(const char* utf8);
  void deleteBackward();
  void applyAlternate(int16_t value);
  void trackDrag(uint32_t now, bool& update);
  void placeCursor(int x, int y);

  int headerHeight() const;
  int textTop() const;
  int textBottom() const;
  int lineHeight(int line) const;
  int lineAtY(int y) const;  // wrapped line under screen y (may be past the last line)
  int maxTopLine() const;    // the top line that brings the last line to the bottom
  int linesFrom(int top) const;
  int advance(uint32_t cp, uint8_t style, uint8_t font);
  int widthAt(size_t i, uint32_t cp, uint8_t font);
  int boxSize() const;
  void restyle();
  bool toggleCheckboxAt(int x, int y);
  void insertNewline();
  void drawGlyph(uint8_t attr, int x, int y, int lineHeight);
  void wrap();
  int lineOf(size_t offset) const;
  void keepCursorVisible();
  void saveNow();

  void drawHeader() const;
  void drawText();
};
