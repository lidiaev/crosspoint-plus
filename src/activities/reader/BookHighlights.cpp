#include "BookHighlights.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/notes/NotesStore.h"
#include "components/icons/selectionIcons.h"

namespace highlights {
namespace {
constexpr size_t MAX_FILE_BYTES = 512 * 1024;
constexpr int UNDERLINE = 2;

int entryAt(const std::vector<Highlight>& entries, const int spine, const uint32_t offset) {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].spine == spine && offset >= entries[i].start && offset < entries[i].end) return static_cast<int>(i);
  }
  return -1;
}
}  // namespace

std::string pathFor(const std::string& bookPath) {
  const size_t slash = bookPath.rfind('/');
  std::string name = slash == std::string::npos ? bookPath : bookPath.substr(slash + 1);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return std::string(DIR) + "/" + name + ".md";
}

bool load(const std::string& bookPath, std::vector<Highlight>& entries) {
  entries.clear();
  const std::string path = pathFor(bookPath);
  if (!Storage.exists(path.c_str())) return true;
  HalFile f;
  if (!Storage.openFileForRead("HLT", path, f)) return false;
  std::string text;
  text.resize(std::min(f.fileSize(), MAX_FILE_BYTES));
  const int n = text.empty() ? 0 : f.read(text.data(), text.size());
  if (n < 0) return false;
  text.resize(static_cast<size_t>(n));
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  std::string title;
  parse(text, title, entries);
  mergeOverlapping(entries);
  return true;
}

bool save(const std::string& bookPath, const std::string& title, std::vector<Highlight>& entries) {
  mergeOverlapping(entries);
  Storage.ensureDirectoryExists(notes::DIR);
  Storage.ensureDirectoryExists(DIR);
  return notes::save(pathFor(bookPath), format(title, entries));
}

void collectMarks(GfxRenderer& renderer, const Page& page, const int fontId, const int marginLeft, const int marginTop,
                  const int spine, const std::vector<Highlight>& entries, std::vector<Mark>& out) {
  out.clear();
  if (entries.empty()) return;

  // Measuring loads glyphs, so first make sure the page has a highlight at all.
  std::string pageText;
  uint8_t styleMask = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine*>(element.get())->getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      if (entryAt(entries, spine, block->wordVisibleOffset(i)) < 0) continue;
      pageText.append(block->wordText(i));
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
    }
  }
  if (pageText.empty()) return;
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);

  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int y = line->yPos + marginTop + block->getRubyShift(renderer.getFontAscenderSize(fontId));
    const int8_t tracking = block->getBlockStyle().characterSpacing;
    Mark run{};
    bool open = false;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const uint32_t offset = block->wordVisibleOffset(i);
      const int entry = entryAt(entries, spine, offset);
      if (open && entry != run.entry) {
        out.push_back(run);
        open = false;
      }
      if (entry < 0) continue;
      const int x = line->xPos + block->wordXpos(i) + marginLeft;
      const int right = x + renderer.getTextAdvanceX(fontId, block->wordText(i), block->wordStyle(i), tracking);
      if (!open) {
        run = Mark{static_cast<int16_t>(x),          static_cast<int16_t>(y),     0,
                   static_cast<int16_t>(lineHeight), static_cast<int16_t>(entry), false};
        open = true;
      }
      run.width = static_cast<int16_t>(right - run.x);
      const bool lastWord =
          i + 1 == block->wordCount() || entryAt(entries, spine, block->wordVisibleOffset(i + 1)) != entry;
      run.endsHighlight = lastWord && offset + block->wordTextLen(i) >= entries[entry].end - 1;
    }
    if (open) out.push_back(run);
  }
}

void drawMarks(const GfxRenderer& renderer, const std::vector<Mark>& marks, const std::vector<Highlight>& entries) {
  const bool invert = SETTINGS.selectionStyle == CrossPointSettings::SELECTION_INVERT;
  for (const Mark& mark : marks) {
    if (invert) {
      renderer.invertRect(mark.x, mark.y, mark.width, mark.height);
    } else {
      renderer.fillRect(mark.x, mark.y + mark.height - UNDERLINE, mark.width, UNDERLINE);
    }
    if (!mark.endsHighlight || entries[mark.entry].comment.empty()) continue;
    const freeink::Icon& badge = icon_sel_badge_14;
    renderer.drawIconUpright(badge.bits, mark.x + mark.width + 1, mark.y, badge.w, badge.h);
  }
}

}  // namespace highlights
