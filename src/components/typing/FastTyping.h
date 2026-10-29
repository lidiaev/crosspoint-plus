#pragma once

#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"

// Fast typing display mode for the X4 Pro UC8279 panel. The hooks live in the
// FreeInk SDK patch scripts/freeink_sdk_patches/0001-uc8279-fast-typing.patch:
// fast refreshes use a short custom LUT and send only the changed regions of
// the frame. Other panels ignore the hooks.
namespace freeink {
extern volatile uint8_t uc8279FastLutFrames;
extern volatile uint8_t uc8279FastLutWhiteFrames;
extern volatile bool uc8279WindowedFast;
}  // namespace freeink

namespace fast_typing {
// Frames to the ink color and back to the background, per "Screen while typing".
// The way back is longer: it erases the previous frame (keys released, cursor moved).
struct Frames {
  uint8_t ink;
  uint8_t background;
};
inline Frames frames() {
  static constexpr Frames BY_SETTING[] = {{8, 16}, {10, 20}, {14, 28}};
  return BY_SETTING[std::min<uint8_t>(SETTINGS.typingRefresh, 2)];
}

// The LUT rails are physical colors. Night mode inverts the frame, so ink is
// white and the background black: the long rail must then drive to black, or
// the background is left grey and noisy.
inline void enable() {
  const Frames f = frames();
  const bool night = SETTINGS.screenInverted != 0;
  freeink::uc8279FastLutFrames = night ? f.background : f.ink;  // to black
  freeink::uc8279FastLutWhiteFrames = night ? f.ink : f.background;
  freeink::uc8279WindowedFast = true;
}

inline void disable() {
  freeink::uc8279FastLutFrames = 0;
  freeink::uc8279FastLutWhiteFrames = 0;
  freeink::uc8279WindowedFast = false;
}

// Pause after the last fast frame before the clean repaint, per "Clean repaint after".
inline uint32_t cleanupDelayMs() {
  static constexpr uint32_t BY_SETTING[] = {600, 1200, 3000};
  return BY_SETTING[std::min<uint8_t>(SETTINGS.typingCleanDelay, 2)];
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
