#include "TouchTracker.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

namespace {
constexpr int MATCH_PX = 48;  // same finger if it moved less than this between samples
constexpr int EDGE_PX = 8;    // a contact landing on the side edge is a gripping thumb
}  // namespace

uint8_t TouchTracker::poll(const GfxRenderer& renderer, const uint32_t now, Press* out, const uint8_t max) {
  HalGPIO::TouchContact contacts[MAX_TRACKS];
  const uint8_t count = gpio.getTouchContacts(contacts, MAX_TRACKS);
  for (Track& t : tracks) t.seen = false;

  const int width = renderer.getScreenWidth();
  for (uint8_t i = 0; i < count; ++i) {
    int x = 0;
    int y = 0;
    renderer.tapToLogical(contacts[i].nx, contacts[i].ny, x, y);
    Track* match = nullptr;
    int best = MATCH_PX * MATCH_PX;
    for (Track& t : tracks) {
      if (!t.active || t.seen) continue;
      const int d = (t.x - x) * (t.x - x) + (t.y - y) * (t.y - y);
      if (d < best) {
        best = d;
        match = &t;
      }
    }
    if (!match) {
      for (Track& t : tracks) {
        if (t.active) continue;
        t = Track{true, false, false, 0, 0, now, INT16_MIN};
        t.committed = x < EDGE_PX || x >= width - EDGE_PX;
        match = &t;
        break;
      }
    }
    if (!match) continue;
    match->x = static_cast<int16_t>(x);
    match->y = static_cast<int16_t>(y);
    match->seen = true;
  }

  uint8_t presses = 0;
  anyActive = false;
  for (uint8_t slot = 0; slot < MAX_TRACKS; ++slot) {
    Track& t = tracks[slot];
    if (!t.active) continue;
    if (!t.seen) {
      t.active = false;
      continue;
    }
    anyActive = true;
    if (!t.committed && presses < max) {
      t.committed = true;
      out[presses] = Press{t.x, t.y, t.since};
      pressSlots[presses] = slot;
      ++presses;
    }
  }
  return presses;
}

bool TouchTracker::heldValue(const int16_t value, Held& held) const {
  for (uint8_t slot = 0; slot < MAX_TRACKS; ++slot) {
    const Track& t = tracks[slot];
    if (t.active && t.committed && t.value == value) {
      held = Held{slot, t.x, t.y, t.since};
      return true;
    }
  }
  return false;
}

bool TouchTracker::position(const uint8_t slot, const uint32_t downMs, int16_t& x, int16_t& y) const {
  if (slot >= MAX_TRACKS) return false;
  const Track& t = tracks[slot];
  if (!t.active || t.since != downMs) return false;
  x = t.x;
  y = t.y;
  return true;
}
