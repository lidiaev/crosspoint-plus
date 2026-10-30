#pragma once

#include <string>
#include <vector>

#include "HighlightFormat.h"

class GfxRenderer;
class Page;

namespace highlights {

inline constexpr const char* DIR = "/notes/highlights";

// /notes/highlights/<book file name>.md
std::string pathFor(const std::string& bookPath);
bool load(const std::string& bookPath, std::vector<Highlight>& entries);
bool save(const std::string& bookPath, const std::string& title, std::vector<Highlight>& entries);

// One highlighted run of a page line, in screen coordinates.
struct Mark {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  int16_t entry;       // index in the highlight list
  bool endsHighlight;  // the highlight's last word is on this run
};

// The page's highlighted runs.
void collectMarks(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop, int spine,
                  const std::vector<Highlight>& entries, std::vector<Mark>& out);
// Draws them in the style from the settings, with a badge where a comment ends.
void drawMarks(const GfxRenderer& renderer, const std::vector<Mark>& marks, const std::vector<Highlight>& entries);

}  // namespace highlights
