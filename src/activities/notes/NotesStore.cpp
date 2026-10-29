#include "NotesStore.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "NoteMarkdown.h"

namespace notes {
namespace {
constexpr size_t TITLE_PEEK_BYTES = 320;
constexpr size_t PREVIEW_BYTES = 120;
constexpr size_t NAME_BYTES = 120;
constexpr size_t TITLE_FILE_CHARS = 48;  // bytes of the title kept in a file name

constexpr const char* PINS_PATH = "/notes/.pinned";

std::string nameOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::vector<std::string> loadPins() {
  std::vector<std::string> pins;
  std::string data;
  if (!Storage.exists(PINS_PATH) || !load(PINS_PATH, data)) return pins;
  pins.reserve(8);
  size_t start = 0;
  while (start < data.size()) {
    size_t end = data.find('\n', start);
    if (end == std::string::npos) end = data.size();
    if (end > start) pins.emplace_back(data, start, end - start);
    start = end + 1;
  }
  return pins;
}

bool savePins(const std::vector<std::string>& pins) {
  std::string data;
  for (const auto& name : pins) data += name + "\n";
  if (data.empty()) return !Storage.exists(PINS_PATH) || Storage.remove(PINS_PATH);
  return save(PINS_PATH, data);
}

// Characters FAT and the web UI reject in names are replaced by spaces.
bool unsafeNameChar(const char c) {
  return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
         static_cast<uint8_t>(c) < 0x20;
}
}  // namespace

namespace {
// The first non-blank line as shown (without markup), and where it ends.
std::string firstLine(const std::string& text, size_t& lineEnd) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = note_md::plainText(text.substr(start, end - start));
    size_t b = 0;
    while (b < line.size() && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r')) ++b;
    size_t e = line.size();
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) --e;
    if (e > b) {
      lineEnd = end;
      return line.substr(b, e - b);
    }
    start = end + 1;
  }
  lineEnd = text.size();
  return {};
}
}  // namespace

std::string titleOf(const std::string& text) {
  size_t end = 0;
  return firstLine(text, end);
}

std::string previewOf(const std::string& text, const size_t maxBytes) {
  size_t i = 0;
  firstLine(text, i);
  std::string out;
  if (i >= text.size()) return out;
  // Markup goes, lines join with spaces; three bytes of source per shown byte is plenty.
  const std::string shown = note_md::plainText(text.substr(i, std::min(text.size() - i, maxBytes * 3)));
  out.reserve(maxBytes);
  for (size_t k = 0; k < shown.size() && out.size() < maxBytes; ++k) {
    const char c = shown[k];
    if (c == '\n' || c == '\t' || c == ' ' || c == '\r') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
      continue;
    }
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
  const std::vector<std::string> pins = loadPins();
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
    info.pinned = std::find(pins.begin(), pins.end(), name) != pins.end();
    out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(), [](const NoteInfo& a, const NoteInfo& b) {
    if (a.pinned != b.pinned) return a.pinned;
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

bool remove(const std::string& path) {
  setPinned(path, false);
  return Storage.remove(path.c_str());
}

bool setPinned(const std::string& path, const bool pinned) {
  std::vector<std::string> pins = loadPins();
  const std::string name = nameOf(path);
  const auto it = std::find(pins.begin(), pins.end(), name);
  if ((it != pins.end()) == pinned) return true;
  if (pinned) {
    pins.push_back(name);
  } else {
    pins.erase(it);
  }
  return savePins(pins);
}

bool move(const std::string& from, const std::string& to, const std::string& text) {
  if (!save(to, text)) return false;
  std::vector<std::string> pins = loadPins();
  const auto it = std::find(pins.begin(), pins.end(), nameOf(from));
  if (it != pins.end()) {
    *it = nameOf(to);
    savePins(pins);
  }
  return Storage.remove(from.c_str());
}

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
