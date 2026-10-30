#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "NoteMarkdown.h"
#include "activities/Activity.h"
#include "components/SelectionCallout.h"
#include "components/typing/TextSelection.h"
#include "components/typing/TouchTracker.h"
#include "components/typing/TypingKeyboard.h"
#include "fontIds.h"

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
  enum class EventType : uint8_t {
    Key,
    Alternate,
    TapText,
    LongPress,
    SelectTo,
    ScrollUp,
    ScrollDown,
    ScrollBy,
    Toolbar,
    Callout
  };
  // The toolbar above the keyboard formats the paragraph or word and hides the
  // keyboard; the callout by the selection edits it.
  enum class Tool : uint8_t { Checkbox, List, Heading, Style, HideKeyboard, Copy, Cut, Paste, SelectAll };
  static constexpr int FORMAT_COUNT = 5;
  static constexpr int MAX_ACTIONS = 4;
  // What a finger on the text does: scroll, or move one end of the selection.
  enum class Drag : uint8_t { Scroll, Select, StartHandle, EndHandle };
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
  Drag dragMode = Drag::Scroll;
  int16_t grabDy = 0;  // from the finger to the line its handle follows
  int16_t sentX = 0;   // last position sent as SelectTo
  int16_t sentY = 0;
  // Where the render task drew the selection handles (a point on each boundary's line), for grabbing them.
  std::atomic<bool> selectionShown{false};
  std::atomic<int16_t> startHandleX{0};
  std::atomic<int16_t> startHandleY{0};
  std::atomic<int16_t> endHandleX{0};
  std::atomic<int16_t> endHandleY{0};
  // The callout's rectangle as last drawn; empty when hidden.
  std::atomic<int16_t> calloutLeft{0};
  std::atomic<int16_t> calloutTop{0};
  std::atomic<int16_t> calloutRight{0};
  std::atomic<int16_t> calloutBottom{0};

  // Render task state.
  std::string path;
  std::string text;
  size_t cursor = 0;             // byte offset, always on a code point boundary
  text_select::Range selection;  // empty = none; the cursor sits at its end
  text_select::Range anchor;     // the word a held finger selected first
  bool pasteMenu = false;        // the callout offers Paste without a selection
  SelectionCallout callout;
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
  // Ubuntu, the UI font: body, then bold heading sizes.
  static constexpr int fonts[FONT_SLOTS] = {UI_12_FONT_ID, UI_14_BOLD_FONT_ID, UI_16_BOLD_FONT_ID};
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
  size_t offsetAt(int x, int y);
  bool hasSelection() const { return !selection.empty(); }
  void clearSelection();
  void eraseSelection();
  void selectWord(int x, int y);
  void selectTo(Drag handle, int x, int y);
  void paste();
  int actions(Tool out[MAX_ACTIONS]) const;
  void applyTool(Tool tool);

  int headerHeight() const;
  int textTop() const;
  int textBottom() const;
  int toolbarTop() const;
  void applyFormat(Tool format);
  void applyStyle();
  text_select::Range styleTarget() const;
  std::string numberedMarker(size_t paragraphStart) const;
  void drawToolbar() const;
  void drawCallout(int startX, int startTop, int endX, int endBottom);
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
  bool autoCapitalize();
  void drawGlyph(uint8_t attr, int x, int y, int rowHeight);
  void wrap();
  int lineOf(size_t offset) const;
  void keepCursorVisible();
  void saveNow();

  void drawHeader() const;
  void drawText();
};
