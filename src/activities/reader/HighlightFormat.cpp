#include "HighlightFormat.h"

#include <algorithm>
#include <cstdio>

namespace highlights {
namespace {
constexpr const char* ANCHOR_PREFIX = "<!-- cp ";

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::string trimmed(const std::string& s) {
  const size_t start = s.find_first_not_of(" \n");
  if (start == std::string::npos) return "";
  return s.substr(start, s.find_last_not_of(" \n") - start + 1);
}

// Keeps the quote on one line, whatever was selected.
std::string oneLine(std::string s) {
  std::replace(s.begin(), s.end(), '\n', ' ');
  return s;
}

bool parseAnchor(const std::string& line, Highlight& entry) {
  int spine = 0;
  unsigned long start = 0;
  unsigned long end = 0;
  unsigned percent = 0;
  if (std::sscanf(line.c_str(), "<!-- cp %d %lu %lu %u -->", &spine, &start, &end, &percent) != 4) return false;
  entry.spine = spine;
  entry.start = static_cast<uint32_t>(start);
  entry.end = static_cast<uint32_t>(end);
  entry.percent = static_cast<uint8_t>(std::min(percent, 100u));
  return true;
}
}  // namespace

void sortByPosition(std::vector<Highlight>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const Highlight& a, const Highlight& b) {
    if ((a.spine < 0) != (b.spine < 0)) return b.spine < 0;
    if (a.spine != b.spine) return a.spine < b.spine;
    return a.start < b.start;
  });
}

std::string format(const std::string& title, const std::vector<Highlight>& entries) {
  std::string out = "# " + title + "\n";
  char anchor[64];
  for (const Highlight& entry : entries) {
    out += "\n> " + oneLine(entry.quote) + "\n";
    if (!entry.comment.empty()) out += "\n" + entry.comment + "\n";
    if (entry.spine >= 0) {
      std::snprintf(anchor, sizeof(anchor), "<!-- cp %d %lu %lu %u -->\n", entry.spine,
                    static_cast<unsigned long>(entry.start), static_cast<unsigned long>(entry.end),
                    static_cast<unsigned>(entry.percent));
      out += anchor;
    }
  }
  return out;
}

void parse(const std::string& text, std::string& title, std::vector<Highlight>& entries) {
  title.clear();
  entries.clear();
  Highlight current;
  bool open = false;
  const auto close = [&]() {
    if (!open) return;
    current.comment = trimmed(current.comment);
    entries.push_back(std::move(current));
    current = Highlight{};
    open = false;
  };
  for (size_t at = 0; at <= text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    if (startsWith(line, "> ")) {
      close();
      current.quote = line.substr(2);
      open = true;
    } else if (open && startsWith(line, ANCHOR_PREFIX) && parseAnchor(line, current)) {
      close();
    } else if (open) {
      current.comment += line + "\n";
    } else if (title.empty() && startsWith(line, "# ")) {
      title = line.substr(2);
    }
  }
  close();
}

}  // namespace highlights
