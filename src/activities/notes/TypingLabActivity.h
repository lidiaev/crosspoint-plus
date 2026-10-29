#pragma once

#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Typing bench (lab builds only, CPX_TYPING_LAB). Keys commit on touch-down and
// reach the panel with a short custom LUT; the cursor and key outline stay off
// the black-to-white path while typing, and an idle factory-waveform repaint
// clears short-LUT residue.
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
  enum class Control : uint8_t { None, Back, Lut, Blink, Clear };

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
  std::atomic<bool> blinkFrame{false};
  std::atomic<bool> blinkEnabled{false};
  std::atomic<uint8_t> lutIndex{1};
  std::atomic<uint32_t> lastKeyFrameAt{0};  // 0 = idle cleanup already done
  std::atomic<uint32_t> cleanupDoneAt{0};
  bool contactActive = false;
  uint32_t nextBlinkAt = 0;
  uint32_t blinkUntil = 0;
  uint32_t blinkStartedFor = 0;

  // Render task state.
  std::string text;
  freeink::ui::KeyboardLayoutId layoutId = freeink::ui::KeyboardLayoutId::QwertyEn;
  bool showLangKey = false;
  bool shifted = false;
  bool symbols = false;
  bool cursorOn = true;
  int16_t outlinedKey = INT16_MIN;
  freeink::ui::InteractionBuffer<56> interactions;
  uint32_t lastLatencyMs = 0;
  uint32_t lastPanelMs = 0;
  uint32_t latencySumMs = 0;
  uint32_t refreshCount = 0;
  uint32_t cleanupCount = 0;

  void enqueue(int16_t value, uint32_t downMs);
  bool keyAt(int x, int y, int16_t& value) const;
  Control controlAt(int x, int y) const;
  int barHeight() const;
  // Applies one key; returns true when the keyboard layout changed.
  bool applyKey(int16_t value);
  const freeink::ui::KeyboardLayout& currentLayout() const;
  freeink::ui::Rect keyboardRect() const;
  void applyLut() const;
  void drawBars() const;
  void drawText(bool cursor) const;
  void drawKeyboard(int16_t outline);
};
