#pragma once

#include <cstdint>

class GfxRenderer;

// Per-finger touch tracking for typing. Each finger presses exactly once, when
// it lands; a finger resting on the screen (a gripping thumb) never presses
// again, and overlapping fingers during fast typing are separate presses. The
// single-contact touch API hides the first finger while a second one is down,
// which made a resting thumb re-press whatever key lay under it.
class TouchTracker {
 public:
  struct Press {
    int16_t x;
    int16_t y;
    uint32_t downMs;
  };
  struct Held {
    uint8_t slot;
    int16_t x;
    int16_t y;
    uint32_t downMs;
  };

  // Samples the contacts. Returns new presses (logical coordinates) in `out`.
  uint8_t poll(const GfxRenderer& renderer, uint32_t now, Press* out, uint8_t max);
  // True while any finger is down.
  bool anyDown() const { return anyActive; }
  // Records which key a press resolved to, so holding it can repeat (Backspace).
  void setPressedValue(uint8_t slot, int16_t value) { tracks[slot].value = value; }
  // Slot of the press returned at index i by the last poll().
  uint8_t pressSlot(uint8_t i) const { return pressSlots[i]; }
  // The finger holding a key with this value, if any.
  bool heldValue(int16_t value, Held& held) const;

 private:
  struct Track {
    bool active;
    bool committed;
    bool seen;
    int16_t x;
    int16_t y;
    uint32_t since;
    int16_t value;
  };
  static constexpr uint8_t MAX_TRACKS = 4;
  Track tracks[MAX_TRACKS] = {};
  uint8_t pressSlots[MAX_TRACKS] = {};
  bool anyActive = false;
};
