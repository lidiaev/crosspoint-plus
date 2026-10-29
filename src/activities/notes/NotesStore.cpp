#include "NotesStore.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace notes {
namespace {
constexpr size_t TITLE_PEEK_BYTES = 320;
constexpr size_t PREVIEW_BYTES = 120;
constexpr size_t NAME_BYTES = 120;
constexpr size_t TITLE_FILE_CHARS = 48;  // bytes of the title kept in a file name

// Characters FAT and the web UI reject in names are replaced by spaces.
bool unsafeNameChar(const char c) {
  return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
         static_cast<uint8_t>(c) < 0x20;
}
}  // namespace

std::string titleOf(const std::string& text) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    size_t b = start;
    while (b < end && (text[b] == '#' || text[b] == ' ' || text[b] == '\t')) ++b;
    size_t e = end;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\r' || text[e - 1] == '\t')) --e;
    if (e > b) return text.substr(b, e - b);
    start = end + 1;
  }
  return {};
}

std::string previewOf(const std::string& text, const size_t maxBytes) {
  // Skip to the end of the title line (the first non-blank one).
  size_t i = 0;
  while (i < text.size() && (text[i] == '\n' || text[i] == ' ' || text[i] == '\t' || text[i] == '#')) ++i;
  i = text.find('\n', i);
  std::string out;
  if (i == std::string::npos) return out;
  out.reserve(std::min(maxBytes, text.size() - i));
  bool lineStart = true;
  for (; i < text.size() && out.size() < maxBytes; ++i) {
    const char c = text[i];
    if (c == '\n' || c == '\t' || c == ' ') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
      lineStart = lineStart || c == '\n';
      continue;
    }
    if (lineStart && c == '#') continue;
    lineStart = false;
    out.push_back(c);
  }
  // Cut back to a whole UTF-8 sequence if the byte budget split one.
  if (out.size() >= maxBytes) {
    size_t cut = out.size();
    while (cut > 0 && (static_cast<uint8_t>(out[cut - 1]) & 0xC0) == 0x80) --cut;
    if (cut > 0 && static_cast<uint8_t>(out[cut - 1]) >= 0xC0) --cut;
    out.resize(cut);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::vector<NoteInfo> list() {
  std::vector<NoteInfo> out;
  Storage.ensureDirectoryExists(DIR);
  auto dir = Storage.open(DIR);
  if (!dir || !dir.isDirectory()) return out;

  char name[NAME_BYTES + 1];
  size_t count = 0;
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) ++count;
  dir.rewindDirectory();
  out.reserve(count);

  char peek[TITLE_PEEK_BYTES + 1];
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    if (name[0] == '.' || !FsHelpers::hasMarkdownExtension(std::string_view{name})) continue;
    const int n = f.read(peek, TITLE_PEEK_BYTES);
    peek[n > 0 ? n : 0] = '\0';
    NoteInfo info;
    info.path = std::string(DIR) + "/" + name;
    const std::string head(peek);
    info.title = titleOf(head);
    info.preview = previewOf(head, PREVIEW_BYTES);
    if (info.title.empty()) {
      info.title.assign(name, strlen(name) - 3);  // file name without ".md"
    }
    info.modified = f.modificationTime();
    out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(), [](const NoteInfo& a, const NoteInfo& b) {
    if (a.modified != b.modified) return a.modified > b.modified;
    return a.path < b.path;
  });
  return out;
}

bool load(const std::string& path, std::string& text) {
  HalFile f;
  if (!Storage.openFileForRead("NOTES", path, f)) return false;
  const size_t size = std::min(f.fileSize(), MAX_NOTE_BYTES);
  text.resize(size);
  const int n = size ? f.read(text.data(), size) : 0;
  if (n < 0) {
    text.clear();
    return false;
  }
  text.resize(static_cast<size_t>(n));
  // Normalize Windows line endings once on load; the editor works with '\n'.
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return true;
}

bool save(const std::string& path, const std::string& text) {
  Storage.ensureDirectoryExists(DIR);
  const std::string tmp = path + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("NOTES", tmp, f)) return false;
    if (f.write(text.data(), text.size()) != text.size()) {
      f.close();
      Storage.remove(tmp.c_str());
      LOG_ERR("NOTES", "Write failed: %s", path.c_str());
      return false;
    }
    if (!f.close()) {
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return false;
  return Storage.rename(tmp.c_str(), path.c_str());
}

bool remove(const std::string& path) { return Storage.remove(path.c_str()); }

std::string pathFor(const std::string& text, const std::string& current) {
  std::string base = titleOf(text);
  if (base.size() > TITLE_FILE_CHARS) {
    size_t cut = TITLE_FILE_CHARS;
    while (cut > 0 && (static_cast<uint8_t>(base[cut]) & 0xC0) == 0x80) --cut;  // keep UTF-8 whole
    base.resize(cut);
  }
  for (char& c : base) {
    if (unsafeNameChar(c)) c = ' ';
  }
  while (!base.empty() && (base.back() == ' ' || base.back() == '.')) base.pop_back();
  if (base.empty()) base = "note";

  std::string candidate = std::string(DIR) + "/" + base + ".md";
  for (int i = 2; candidate != current && Storage.exists(candidate.c_str()); ++i) {
    candidate = std::string(DIR) + "/" + base + " " + std::to_string(i) + ".md";
  }
  return candidate;
}

}  // namespace notes
