#pragma once

// Shares reading positions of books from the Git library (/books/Books/files/)
// with other devices through /vault/Books/sync/, the format of the Obsidian
// Xteink Book Sync plugin. The vault's Git sync carries the files both ways.

#include <ReadingSync.h>

#include <string>

namespace ReadingSyncBridge {

// Resolves a book opened from the library to its catalog entry.
bool resolve(const std::string& bookPath, readingsync::BookRef& book);

// Records this device's position (fraction of the book, and the source PDF
// page for XTC copies of a PDF, else 0). Skipped without a set clock.
void exportPosition(const std::string& bookPath, double pct, int page);

// A position from another device that is newer than this device's own.
bool newerElsewhere(const std::string& bookPath, readingsync::BookRef& book, readingsync::Position& position);

}  // namespace ReadingSyncBridge
