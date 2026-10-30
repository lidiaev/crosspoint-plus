#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A book's highlights as one Markdown file: readable and editable anywhere,
// with each anchor in an HTML comment that Markdown does not show:
//
//   # Book title
//
//   > Highlighted text.
//
//   Comment.
//   <!-- cp 4 1200 1260 37 -->
//
// The anchor is the spine index, the visible-codepoint range in that chapter and
// the book percentage. The range survives font and layout changes.
namespace highlights {

struct Highlight {
  int spine = -1;  // -1 = no anchor: written by hand, listed but not shown in the book
  uint32_t start = 0;
  uint32_t end = 0;  // exclusive
  uint8_t percent = 0;
  std::string quote;
  std::string comment;
};

// Book order; entries without an anchor last.
void sortByPosition(std::vector<Highlight>& entries);
std::string format(const std::string& title, const std::vector<Highlight>& entries);
void parse(const std::string& text, std::string& title, std::vector<Highlight>& entries);

}  // namespace highlights
