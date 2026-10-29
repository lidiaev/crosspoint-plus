#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Notes are plain Markdown files in /notes. The file is the source of truth:
// notes edited over Wi-Fi or USB show up as-is.
namespace notes {

inline constexpr const char* DIR = "/notes";
inline constexpr size_t MAX_NOTE_BYTES = 32 * 1024;

struct NoteInfo {
  std::string path;
  std::string title;    // first line without Markdown heading marks
  std::string preview;  // start of the text after the title, on one line
  uint32_t modified;    // FAT timestamp, 0 when unknown
};

// All notes, most recently modified first.
std::vector<NoteInfo> list();
bool load(const std::string& path, std::string& text);
// Writes via "<path>.tmp" and a rename, so an interrupted save keeps the old file.
bool save(const std::string& path, const std::string& text);
bool remove(const std::string& path);
// Title shown for a note: its first non-empty line, without leading '#'.
std::string titleOf(const std::string& text);
// Up to maxBytes of the text after the title line, newlines folded to spaces.
std::string previewOf(const std::string& text, size_t maxBytes);
// A free "/notes/<title>.md" path for the text, keeping `current` when it already matches.
std::string pathFor(const std::string& text, const std::string& current);

}  // namespace notes
