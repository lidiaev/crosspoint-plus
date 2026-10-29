#pragma once

#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Measurement bench for touch typing latency (lab builds only, CPX_TYPING_LAB).
// The keyboard is drawn once; each strategy differs only in how a key press
// reaches the panel, and the top line shows the measured times.
class TypingLabActivity final : public Activity {
 public:
  enum class Strategy : uint8_t {
    Stock,   // highlight on touch-down, commit on release, whole frame redrawn
    Full,    // commit on touch-down, text redrawn, whole-frame fast refresh
    Window,  // commit on touch-down, text redrawn, windowed refresh of the text band
    Count
  };

  TypingLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TypingLab", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  struct PendingKey {
    int16_t value;
    uint32_t downMs;
  };
  static constexpr uint8_t QUEUE_SIZE = 32;

  // Main task (loop) produces, render task consumes.
  PendingKey queue[QUEUE_SIZE] = {};
  std::atomic<uint8_t> queueHead{0};
  std::atomic<uint8_t> queueTail{0};
  std::atomic<int16_t> highlightValue{INT16_MIN};
  std::atomic<bool> fullRedraw{true};
  std::atomic<uint8_t> strategy{static_cast<uint8_t>(Strategy::Full)};
  uint32_t stockDownMs = 0;
  int16_t stockDownValue = INT16_MIN;
  bool contactActive = false;  // a contact was already handled; wait for lift-off
  // Last typed key stays highlighted until the next key or an idle cleanup frame.
  std::atomic<uint32_t> highlightShownAt{0};
  std::atomic<bool> clearHighlight{false};
  int16_t typedHighlight = INT16_MIN;

  // Render task state.
  std::string text;
  freeink::ui::KeyboardLayoutId layoutId = freeink::ui::KeyboardLayoutId::QwertyEn;
  bool showLangKey = false;
  bool shifted = false;
  bool symbols = false;
  freeink::ui::InteractionBuffer<56> interactions;
  uint32_t lastLatencyMs = 0;
  uint32_t lastDrawMs = 0;
  uint32_t lastPanelMs = 0;
  uint32_t latencySumMs = 0;
  uint32_t refreshCount = 0;
  uint32_t keyCount = 0;
  uint8_t lastBatch = 0;

  enum class Control : uint8_t { None, Back, Mode, Clear, Lut, Pll };
  std::atomic<uint8_t> pllIndex{0};
  std::atomic<uint8_t> lutIndex{3};  // 8 frames
  uint32_t cleanupCount = 0;

  void enqueue(int16_t value, uint32_t downMs);
  Control controlAt(int x, int y) const;
  int controlBarHeight() const;
  bool keyAt(int x, int y, int16_t& value) const;
  // Applies one key to the text and layout; returns true when the keyboard must be redrawn.
  bool applyKey(int16_t value);
  const freeink::ui::KeyboardLayout& currentLayout() const;
  freeink::ui::Rect keyboardRect() const;
  int textBandBottom() const;
  void drawStats() const;
  void drawText() const;
  void drawKeyboard(int16_t highlight);
};
