#pragma once

#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Typing bench (lab builds only, CPX_TYPING_LAB). Keys commit on touch-down and
// reach the panel with a short custom LUT; the pressed key is inverted until
// lift-off, the cursor is hidden while typing, and an idle factory-waveform
// repaint clears short-LUT residue.
class TypingLabActivity final : public Activity {
 public:
  TypingLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TypingLab", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  enum class Control : uint8_t { None, Back, Hit, Scan, Clear };

  static constexpr uint8_t KB_ROWS = 4;
  static constexpr uint8_t KB_COLS = 14;

  struct PendingKey {
    int16_t value;
    uint32_t downMs;
  };
  static constexpr uint8_t QUEUE_SIZE = 32;
  static constexpr int16_t CLEAR_TEXT = INT16_MIN;

  // Main task (loop) produces, render task consumes.
  PendingKey queue[QUEUE_SIZE] = {};
  std::atomic<uint8_t> queueHead{0};
  std::atomic<uint8_t> queueTail{0};
  std::atomic<bool> fullRedraw{true};
  std::atomic<bool> releaseFrame{false};
  std::atomic<bool> cleanupFrame{false};
  std::atomic<uint8_t> lutIndex{2};         // 8/16
  std::atomic<uint32_t> lastKeyFrameAt{0};  // 0 = idle cleanup already done
  std::atomic<uint32_t> pressedShownAt{0};  // 0 = no key shown inverted
  std::atomic<bool> windowed{true};
  std::atomic<bool> hitDelay{false};  // resolve the key ~30 ms after touch-down
  std::atomic<bool> showDot{true};    // mark where the last key press registered
  std::atomic<bool> scanWindow{false};
  std::atomic<int16_t> dotX{-1};
  std::atomic<int16_t> dotY{-1};
  // One track per finger: each presses exactly once, when it lands (or after the
  // hit delay). A finger resting on the screen never presses again.
  struct Track {
    bool active;
    bool committed;
    bool seen;
    int16_t x;
    int16_t y;
    uint32_t since;
    int16_t value;  // key it pressed, for Backspace repeat
    uint32_t nextRepeatAt;
  };
  static constexpr uint8_t MAX_TRACKS = 4;
  Track tracks[MAX_TRACKS] = {};
  uint32_t liftedAt = 0;
  bool contactActive = false;

  // Render task state.
  std::string text;
  freeink::ui::KeyboardLayoutId layoutId = freeink::ui::KeyboardLayoutId::QwertyEn;
  bool showLangKey = false;
  bool shifted = false;
  bool symbols = false;
  bool capsLock = false;
  uint32_t lastShiftAt = 0;
  // Letter layers built at runtime: Shift/Caps state is drawn on the key.
  freeink::ui::KeyboardKey kbKeys[KB_ROWS][KB_COLS] = {};
  freeink::ui::KeyboardRow kbRows[KB_ROWS] = {};
  freeink::ui::KeyboardLayout kbLayout = {};
  int16_t pressedKey = INT16_MIN;
  freeink::ui::InteractionBuffer<56> interactions;
  uint32_t lastLatencyMs = 0;
  uint32_t lastPanelMs = 0;
  uint32_t latencySumMs = 0;
  uint32_t refreshCount = 0;
  uint32_t cleanupCount = 0;
  uint32_t keyCount = 0;
  // Driver stage times of the last key frame (ms) and its dirty window count.
  uint32_t keyStream = 0, keySetup = 0, keyDrf = 0, keySync = 0;
  uint8_t keyWindows = 0;

  void enqueue(int16_t value, uint32_t downMs);
  bool keyAt(int x, int y, int16_t& value) const;
  // Handles a top-bar control or queues the key at (x, y); returns the key or INT16_MIN.
  int16_t commitPress(int x, int y, uint32_t downMs);
  void pollTouch(uint32_t now);
  Control controlAt(int x, int y) const;
  int barHeight() const;
  // Applies one key; returns true when the keyboard layout changed.
  bool applyKey(int16_t value, uint32_t downMs);
  void buildLetterLayout();
  void autoCapitalize();
  const freeink::ui::KeyboardLayout& currentLayout() const;
  freeink::ui::Rect keyboardRect() const;
  void applyLut() const;
  void drawBars() const;
  void drawText(bool cursor) const;
  void drawKeyboard(int16_t pressed);
};
