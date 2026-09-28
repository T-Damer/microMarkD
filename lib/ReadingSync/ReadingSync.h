#pragma once

// Reading positions shared with Obsidian through the vault (Xteink Book Sync).
// Every device writes only Books/sync/<bookId>/<deviceId>.json; readers take the
// newest position across all device files for a book. Pure C++ so it runs in
// host tests; file access stays in the firmware glue.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace readingsync {

// Positions stamped before this (2024-01-01) come from an unset clock and
// cannot be ordered against other devices.
constexpr int64_t MIN_VALID_TIME_MS = 1704067200000LL;

struct BookRef {
  std::string id;
  std::string title;
  bool pdf = false;   // PDF read as XTC: `page` is the source PDF page
  int firstPage = 1;  // first PDF page of this XTC volume
  bool multiVolume = false;
};

// Supplies the next catalog bytes into `buffer`; 0 at the end. The catalog grows
// with the library (hundreds of KB), so it is scanned, never held whole: only
// one book entry is parsed at a time.
struct CatalogReader {
  void* context;
  size_t (*read)(void* context, char* buffer, size_t capacity);
};

// Finds the catalog entry owning `filesPath`, a path on the books branch such
// as "Books/files/medical/Title.xtc" (the source or one of its Xteink copies).
bool findBook(const CatalogReader& read, std::string_view filesPath, BookRef& out);
bool findBook(std::string_view catalogJson, std::string_view filesPath, BookRef& out);

// "owner/name" of the GitHub repository whose `books` branch holds the book
// files (catalog "files.repository"); empty when they live in the vault itself.
std::string filesRepository(const CatalogReader& read);
std::string filesRepository(std::string_view catalogJson);

struct Position {
  int64_t updatedAt = 0;  // milliseconds since the epoch
  double pct = 0;         // [0, 1]
  int page = 0;           // one-based source page, 0 when unknown
  std::string deviceId;
};

// Reads the position from one device file; false when the file does not
// belong to this book and device or carries no position.
bool readPosition(std::string_view json, const std::string& bookId, const std::string& deviceId, Position& out);

struct DeviceFile {
  std::string deviceId;  // file name without ".json"
  std::string json;
};

// Newest position among files written by other devices.
bool newestFromOthers(const std::vector<DeviceFile>& files, const std::string& bookId, const std::string& selfId,
                      Position& out);

// This device's file with `position`, keeping anything else the previous
// version held (the format also carries highlights from Obsidian).
std::string writeDeviceFile(const BookRef& book, const std::string& deviceId, const Position& position,
                            std::string_view previousJson);

}  // namespace readingsync
