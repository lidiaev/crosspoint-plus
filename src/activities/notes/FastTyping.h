#pragma once

#include <cstdint>

// Fast typing display mode for the X4 Pro UC8279 panel. The hooks live in the
// FreeInk SDK patch scripts/freeink_sdk_patches/0001-uc8279-fast-typing.patch:
// fast refreshes use a short custom LUT (8 frames to black, 16 to white) and
// send only the changed regions of the frame. Other panels ignore the hooks.
namespace freeink {
extern volatile uint8_t uc8279FastLutFrames;
extern volatile uint8_t uc8279FastLutWhiteFrames;
extern volatile bool uc8279WindowedFast;
}  // namespace freeink

namespace fast_typing {
inline constexpr uint8_t LUT_TO_BLACK = 8;
inline constexpr uint8_t LUT_TO_WHITE = 16;

inline void enable() {
  freeink::uc8279FastLutFrames = LUT_TO_BLACK;
  freeink::uc8279FastLutWhiteFrames = LUT_TO_WHITE;
  freeink::uc8279WindowedFast = true;
}

inline void disable() {
  freeink::uc8279FastLutFrames = 0;
  freeink::uc8279FastLutWhiteFrames = 0;
  freeink::uc8279WindowedFast = false;
}

// RAII: one refresh with the factory waveform and a full transfer. Clears the
// residue short-LUT frames leave and resyncs the controller planes.
class CleanRefreshScope {
 public:
  CleanRefreshScope() { disable(); }
  ~CleanRefreshScope() { enable(); }
  CleanRefreshScope(const CleanRefreshScope&) = delete;
  CleanRefreshScope& operator=(const CleanRefreshScope&) = delete;
};
}  // namespace fast_typing
