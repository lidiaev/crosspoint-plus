#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "components/SelectionCallout.h"
#include "components/typing/TouchTracker.h"
#include "components/typing/TypingKeyboard.h"

// Search shows a magnifier on Return; the others show a check mark.
enum class InputType { Text, Password, Url, Search };

// System text entry: the notes keyboard (keys commit on touch-down, fast
// typing display mode) under a system header with Done. Return also submits.
class KeyboardEntryActivity : public Activity {
 public:
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "",
                                 const size_t maxLength = 0, InputType inputType = InputType::Text)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        maxLength(maxLength),
        inputType(inputType) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override;

 private:
  enum class EventType : uint8_t { Key, Alternate, TogglePassword, TapField, HoldField, Callout, Done };
  enum class Action : uint8_t { Copy, Cut, Paste, SelectAll };
  static constexpr int MAX_ACTIONS = 4;
  struct Event {
    EventType type;
    int16_t value;
    uint32_t downMs;
    int16_t x = 0;
    int16_t y = 0;
  };
  static constexpr uint8_t QUEUE_SIZE = 32;

  std::string title;
  size_t maxLength;
  InputType inputType;

  // Input task (loop) produces events, the render task consumes them.
  Event queue[QUEUE_SIZE] = {};
  std::atomic<uint8_t> queueHead{0};
  std::atomic<uint8_t> queueTail{0};
  std::atomic<bool> fullRedraw{true};
  std::atomic<bool> cleanupFrame{false};
  std::atomic<bool> releaseFrame{false};
  std::atomic<bool> done{false};
  std::atomic<uint32_t> lastEditFrameAt{0};
  std::atomic<uint32_t> pressedShownAt{0};
  TouchTracker touch;
  uint32_t lastInputAt = 0;  // last touch or key, for skipLoopDelay
  uint32_t liftedAt = 0;
  bool wasDown = false;
  uint32_t nextRepeatAt = 0;
  uint32_t repeatFor = 0;
  uint32_t alternateFor = 0;
  // Tap rects drawn by the render task; a torn read misroutes at most one tap.
  int doneX = 0;
  int doneW = 0;
  int fieldTop = 0;
  int fieldBottom = 0;
  int calloutLeft = 0;
  int calloutTop = 0;
  int calloutRight = 0;
  int calloutBottom = 0;
  // A finger held on the field opens the callout.
  int8_t holdSlot = -1;
  uint32_t holdDownMs = 0;
  int16_t holdX = 0;
  int16_t holdY = 0;

  // Render task state.
  std::string text;
  size_t cursor = 0;  // byte offset, always on a code point boundary
  int firstLine = 0;  // first wrapped line in view
  bool passwordVisible = false;
  bool allSelected = false;  // Select all: typing replaces the text
  bool editOffer = false;    // the callout is open
  SelectionCallout callout;
  TypingKeyboard keyboard;
  int16_t pressedKey = INT16_MIN;
  std::vector<uint32_t> lineStarts;

  void enqueue(const Event& event);
  bool popEvent(Event& event);
  void handleEvent(const Event& event);
  void insertText(const char* utf8);
  void deleteBackward();
  std::string shownText() const;  // the text as drawn: masked for a hidden password
  size_t toShown(size_t offset) const;
  size_t fromShown(size_t offset) const;
  void wrapField(const std::string& shown);
  void placeCursor(int x, int y);
  void drawField();
  int actions(Action out[MAX_ACTIONS]) const;
  void applyAction(Action action);
  void drawCallout(int startX, int startTop, int endX, int endBottom);
  void onComplete(std::string result);
  void onCancel();
};
