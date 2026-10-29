#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "TouchTracker.h"
#include "TypingKeyboard.h"
#include "activities/Activity.h"

// Plain-text editor for one note. Keys commit on touch-down and reach the panel
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
  bool skipLoopDelay() override { return true; }

 private:
  enum class EventType : uint8_t { Key, TapText, ToggleKeyboard, ScrollUp, ScrollDown, Back };
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
  uint32_t liftedAt = 0;
  bool wasDown = false;
  uint32_t nextRepeatAt = 0;
  uint32_t repeatFor = 0;  // downMs of the Backspace press being repeated

  // Render task state.
  std::string path;
  std::string text;
  size_t cursor = 0;  // byte offset, always on a code point boundary
  bool dirty = false;
  TypingKeyboard keyboard;
  int16_t pressedKey = INT16_MIN;
  std::vector<uint32_t> lineStarts;  // byte offset of each wrapped line
  int topLine = 0;
  uint8_t widthCache[0x460] = {};  // advance per code point below U+0460, 0 = unknown

  void enqueue(const Event& event);
  bool popEvent(Event& event);
  void handleEvent(const Event& event, bool& layoutChanged);
  void applyKey(int16_t value, uint32_t downMs, bool& layoutChanged);
  void insertText(const char* utf8);
  void deleteBackward();
  void placeCursor(int x, int y);

  int headerHeight() const;
  int textTop() const;
  int textBottom() const;
  int visibleLines() const;
  int advance(uint32_t cp);
  void wrap();
  int lineOf(size_t offset) const;
  void keepCursorVisible();
  void saveNow();

  void drawHeader() const;
  void drawText();
};
