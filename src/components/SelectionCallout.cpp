#include "SelectionCallout.h"

#include <GfxRenderer.h>

#include <algorithm>

namespace {
constexpr int ITEM_WIDTH = 60;
constexpr int HEIGHT = 48;
constexpr int RADIUS = 12;
constexpr int POINTER = 9;  // height of the pointer triangle
constexpr int GAP = 14;     // from the line to the pointer tip: room for the handle knob
constexpr int EDGE = 8;     // from the screen edges
}  // namespace

void SelectionCallout::place(const int items, const int startX, const int startTop, const int endX,
                             const int endBottom, const int areaTop, const int areaBottom, const int screenWidth) {
  count = std::clamp(items, 0, MAX_ITEMS);
  if (count == 0) return;
  const int width = count * ITEM_WIDTH;
  const bool oneLine = endBottom - startTop < 2 * HEIGHT && startX <= endX;
  const int anchorX = oneLine ? (startX + endX) / 2 : screenWidth / 2;
  x = std::clamp(anchorX - width / 2, EDGE, screenWidth - EDGE - width);
  pointerX = std::clamp(anchorX, x + RADIUS + POINTER, x + width - RADIUS - POINTER);
  pointer = true;
  const int above = startTop - GAP - POINTER - HEIGHT;
  const int below = endBottom + GAP + POINTER;
  if (above >= areaTop) {
    y = above;
    pointerUp = false;
  } else if (below + HEIGHT <= areaBottom) {
    y = below;
    pointerUp = true;
  } else {
    y = areaTop + EDGE;
    pointer = false;
  }
}

void SelectionCallout::draw(const GfxRenderer& renderer, const freeink::Icon* const icons[]) const {
  if (count == 0) return;
  const int width = count * ITEM_WIDTH;
  renderer.fillRoundedRect(x, y, width, HEIGHT, RADIUS, Color::Black);
  if (pointer) {
    const int baseY = pointerUp ? y : y + HEIGHT;
    const int tipY = pointerUp ? y - POINTER : y + HEIGHT + POINTER;
    const int xs[3] = {pointerX - POINTER, pointerX + POINTER, pointerX};
    const int ys[3] = {baseY, baseY, tipY};
    renderer.fillPolygon(xs, ys, 3, true);
  }
  for (int i = 0; i < count; ++i) {
    const freeink::Icon& icon = *icons[i];
    const int cx = x + i * ITEM_WIDTH + ITEM_WIDTH / 2;
    renderer.drawIconUpright(icon.bits, cx - icon.w / 2, y + (HEIGHT - icon.h) / 2, icon.w, icon.h, false);
    if (i > 0) renderer.fillRect(x + i * ITEM_WIDTH, y + 12, 1, HEIGHT - 24, false);
  }
}

bool SelectionCallout::contains(const int px, const int py) const {
  return count > 0 && px >= x && px < x + count * ITEM_WIDTH && py >= y && py < y + HEIGHT;
}

int SelectionCallout::itemAt(const int px, const int py) const {
  if (!contains(px, py)) return -1;
  return (px - x) / ITEM_WIDTH;
}

void SelectionCallout::bounds(int& left, int& top, int& right, int& bottom) const {
  left = x;
  top = y;
  right = count > 0 ? x + count * ITEM_WIDTH : x;
  bottom = count > 0 ? y + HEIGHT : y;
}
