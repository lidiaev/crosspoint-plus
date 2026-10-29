#pragma once

#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>

class GfxRenderer;

// Touch keyboard for typing notes: iPhone-like Russian/English letter layers
// plus one symbols layer, drawn with plain labels and outline glyphs. Key ids
// are Unicode code points for characters and the fui::QWERTY_KEY_* ids for
// control keys. Layer state is owned by the render task; the hit table is
// published so the input task can resolve touches lock-free.
class TypingKeyboard {
 public:
  enum class Action : uint8_t { None, Insert, Backspace, Enter, LayoutChanged };

  void begin(bool english);
  // Height for a screen width; the keyboard sits at the bottom of the screen.
  static int height();
  freeink::ui::Rect rect(const GfxRenderer& renderer) const;

  // Render task: applies a pressed key to the layer state. For Insert, `out`
  // receives the UTF-8 text (the character the key showed when pressed).
  Action press(int16_t value, uint32_t downMs, char out[4]);
  // Render task: capitalizes the next letter at the start of a sentence.
  // `before` is the text preceding the cursor. Returns true when the layer changed.
  bool autoCapitalize(const std::string& before, size_t cursor);
  // Render task: draws the keyboard (pressed key inverted) and publishes hit rects.
  void draw(const GfxRenderer& renderer, int16_t pressed);

  // Input task: the key nearest to (x, y), if the point is on the keyboard.
  bool keyAt(int x, int y, int16_t& value) const;
  // The character a long press on this key types instead (е → ё), or 0.
  static int16_t alternateOf(int16_t value);
  // Turns the two-byte character ending at `end` into its alternate (е → ё).
  static bool replaceWithAlternate(std::string& text, size_t end);
  static uint16_t lowerOf(uint16_t cp);
  static uint16_t upperOf(uint16_t cp);

 private:
  static constexpr uint8_t ROWS = 4;
  static constexpr uint8_t COLS = 14;

  void build();

  bool english = false;
  bool shifted = false;
  bool capsLock = false;
  bool symbols = false;
  bool symbolsMore = false;  // the second symbols page (#+=)
  uint32_t lastShiftAt = 0;
  char glyphs[ROWS][COLS][4] = {};
  freeink::ui::KeyboardKey keys[ROWS][COLS] = {};
  freeink::ui::KeyboardRow rows[ROWS] = {};
  freeink::ui::KeyboardLayout layout = {};
  freeink::ui::InteractionBuffer<56> hits;
};
