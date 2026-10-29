#pragma once

#include <Icon.h>

class GfxRenderer;

// The action bar that floats next to a text selection, like the iPhone's edit
// menu: a black pill of white icons with a pointer at the selection. It sits
// above the selection's first line, below its last one when there is no room,
// and over the top of the area when neither fits. Shared by notes and books.
class SelectionCallout {
 public:
  static constexpr int MAX_ITEMS = 5;

  // The selection runs from (startX, startTop) on its first line to (endX,
  // endBottom) on its last; the callout stays inside [areaTop, areaBottom).
  void place(int items, int startX, int startTop, int endX, int endBottom, int areaTop, int areaBottom,
             int screenWidth);
  void hide() { count = 0; }
  bool shown() const { return count > 0; }
  void draw(const GfxRenderer& renderer, const freeink::Icon* const icons[]) const;
  // The item under a point, or -1.
  int itemAt(int px, int py) const;
  bool contains(int px, int py) const;
  // The callout's rectangle, for hit tests on another task.
  void bounds(int& left, int& top, int& right, int& bottom) const;

 private:
  int count = 0;
  int x = 0;
  int y = 0;
  int pointerX = 0;
  bool pointerUp = false;  // the callout is below the selection
  bool pointer = true;     // false when it covers the selection
};
