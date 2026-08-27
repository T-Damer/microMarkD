#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes

#ifdef MICROMARKD_APP
constexpr uint32_t MARKDOWN_CACHE_MAGIC = 0x4D444958;  // "MDIX"
constexpr uint8_t MARKDOWN_CACHE_VERSION = 1;

int markdownIndent(const micromarkd::BlockKind block) {
  switch (block) {
    case micromarkd::BlockKind::Quote:
      return 16;
    case micromarkd::BlockKind::Bullet:
      return 22;
    case micromarkd::BlockKind::OrderedList:
    case micromarkd::BlockKind::Code:
      return 12;
    case micromarkd::BlockKind::Paragraph:
    case micromarkd::BlockKind::Heading:
    case micromarkd::BlockKind::Separator:
      return 0;
  }
  return 0;
}

EpdFontFamily::Style markdownStyle(const micromarkd::ParsedLine& line) {
  if (line.bold) return EpdFontFamily::BOLD;
  if (line.italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

size_t nextUtf8Boundary(const std::string& text, size_t position) {
  if (position >= text.size()) return text.size();
  position++;
  while (position < text.size() && (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80) position++;
  return position;
}

size_t previousUtf8Boundary(const std::string& text, size_t position, const size_t floor) {
  position = std::min(position, text.size());
  while (position > floor && position < text.size() && (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80) {
    position--;
  }
  return position;
}

size_t findMarkdownWrapEnd(GfxRenderer& renderer, const int fontId, const micromarkd::ParsedLine& line,
                           const size_t start, const int availableWidth) {
  if (start >= line.text.size()) return line.text.size();

  const auto fits = [&](const size_t end) {
    const std::string candidate = line.text.substr(start, end - start);
    return renderer.getTextAdvanceX(fontId, candidate.c_str(), markdownStyle(line)) <= availableWidth;
  };

  if (fits(line.text.size())) return line.text.size();

  const size_t minimumEnd = nextUtf8Boundary(line.text, start);
  if (minimumEnd <= start) return line.text.size();

  size_t low = minimumEnd;
  size_t high = line.text.size();
  size_t best = start;

  // Search by raw byte positions, but only measure complete UTF-8 prefixes. Moving
  // the raw midpoint (rather than the aligned candidate) guarantees progress even
  // when several midpoint values fall inside the same multibyte code point.
  while (low <= high) {
    const size_t midpoint = low + (high - low) / 2;
    size_t candidate = previousUtf8Boundary(line.text, midpoint, start);
    if (candidate < minimumEnd) candidate = minimumEnd;

    if (fits(candidate)) {
      best = std::max(best, candidate);
      if (midpoint >= line.text.size()) break;
      low = midpoint + 1;
    } else {
      if (midpoint == 0) break;
      high = midpoint - 1;
    }
  }

  if (best <= start) best = minimumEnd;

  // Alignment can make the binary search stop one boundary early. Advance over
  // any remaining fitting code points before looking for a word boundary.
  while (best < line.text.size()) {
    const size_t candidate = nextUtf8Boundary(line.text, best);
    if (candidate <= best || !fits(candidate)) break;
    best = candidate;
  }

  size_t breakPosition = best;
  while (breakPosition > start) {
    size_t previous = breakPosition - 1;
    while (previous > start && (static_cast<unsigned char>(line.text[previous]) & 0xC0) == 0x80) previous--;
    // Never return the current cursor merely because the fragment begins with
    // whitespace; the caller skips separators after a non-empty fragment.
    if (previous > start && (line.text[previous] == ' ' || line.text[previous] == '\t')) return previous;
    breakPosition = previous;
  }

  return best;
}
#endif
}  // namespace

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    return false;
  }
  txt->setupCacheDir();
#ifdef MICROMARKD_APP
  markdownMode = FsHelpers::hasMarkdownExtension(bookPath);
  currentMarkdownLines.reserve(64);
  markdownPageTextOffsets.reserve(64);
  markdownLinkHits.reserve(24);
  markdownHistory.reserve(8);
#endif
  return true;
}

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex(renderer);
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

#ifdef MICROMARKD_APP
  if (pendingMarkdownPage >= 0) {
    currentPage = std::min(pendingMarkdownPage, std::max(totalPages - 1, 0));
    pendingMarkdownPage = -1;
  }
#endif

  initialized = true;
}

void TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
#ifdef MICROMARKD_APP
  if (markdownMode) {
    pageOffsets.clear();
    markdownPageTextOffsets.clear();

    size_t sourceOffset = 0;
    size_t textOffset = 0;
    const size_t fileSize = txt->getFileSize();

    LOG_DBG("TRS", "Building measured Markdown page index for %zu bytes...", fileSize);
    GUI.drawPopup(renderer, tr(STR_INDEXING));

    if (fileSize == 0) {
      pageOffsets.push_back(0);
      markdownPageTextOffsets.push_back(0);
    }

    while (sourceOffset < fileSize) {
      pageOffsets.push_back(sourceOffset);
      markdownPageTextOffsets.push_back(textOffset);

      std::vector<std::string> tempLines;
      std::vector<micromarkd::ParsedLine> tempMarkdownLines;
      size_t nextSourceOffset = sourceOffset;
      size_t nextTextOffset = textOffset;
      if (!loadMarkdownPageAtCursor(renderer, sourceOffset, textOffset, tempLines, tempMarkdownLines, nextSourceOffset,
                                    nextTextOffset)) {
        break;
      }

      if (nextSourceOffset == sourceOffset && nextTextOffset == textOffset) {
        LOG_ERR("TRS", "Markdown paginator made no progress at %zu:%zu", sourceOffset, textOffset);
        break;
      }

      sourceOffset = nextSourceOffset;
      textOffset = nextTextOffset;
      if (pageOffsets.size() % 20 == 0) vTaskDelay(1);
    }

    totalPages = pageOffsets.size();
    LOG_DBG("TRS", "Built measured Markdown page index: %d pages", totalPages);
    return;
  }
#endif

  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                                         size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);
    size_t lineBytePos = 0;

    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      int lineWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextAdvanceX(cachedFontId, line.substr(0, breakPos).c_str(),
                                                      EpdFontFamily::REGULAR) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    if (line.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);
  return !outLines.empty();
}

#ifdef MICROMARKD_APP
bool TxtReaderActivity::loadMarkdownPageAtCursor(GfxRenderer& renderer, const size_t sourceOffset,
                                                 const size_t textOffset, std::vector<std::string>& outLines,
                                                 std::vector<micromarkd::ParsedLine>& outMarkdownLines,
                                                 size_t& nextSourceOffset, size_t& nextTextOffset) {
  outLines.clear();
  outMarkdownLines.clear();

  const size_t fileSize = txt->getFileSize();
  if (sourceOffset >= fileSize) return false;

  size_t cursorSourceOffset = sourceOffset;
  size_t cursorTextOffset = textOffset;

  // A page normally needs only one 8 KiB read. If an unusually markup-heavy
  // chunk produces fewer visible lines, continue with the next sequential chunk.
  while (cursorSourceOffset < fileSize && static_cast<int>(outLines.size()) < linesPerPage) {
    const size_t chunkLimit = std::min(CHUNK_SIZE, fileSize - cursorSourceOffset);
    const size_t probeSize = std::min(chunkLimit + 1, fileSize - cursorSourceOffset);
    auto* buffer = static_cast<uint8_t*>(malloc(probeSize));
    if (!buffer) {
      LOG_ERR("TRS", "Failed to allocate %zu bytes for Markdown pagination", probeSize);
      break;
    }

    if (!txt->readContent(buffer, cursorSourceOffset, probeSize)) {
      free(buffer);
      break;
    }

    size_t pos = 0;
    while (pos < chunkLimit && static_cast<int>(outLines.size()) < linesPerPage) {
      const size_t lineStart = pos;
      size_t lineEnd = pos;
      while (lineEnd < chunkLimit && buffer[lineEnd] != '\n') lineEnd++;

      bool hasNewline = lineEnd < chunkLimit && buffer[lineEnd] == '\n';
      if (!hasNewline && lineEnd == chunkLimit && probeSize > chunkLimit && buffer[lineEnd] == '\n') {
        hasNewline = true;
      }

      size_t displayLength = lineEnd - lineStart;
      if (displayLength > 0 && buffer[lineStart + displayLength - 1] == '\r') displayLength--;
      const std::string sourceLine(reinterpret_cast<const char*>(buffer + lineStart), displayLength);

      size_t followingSourceOffset = cursorSourceOffset + lineEnd + (hasNewline ? 1 : 0);
      if (followingSourceOffset <= cursorSourceOffset + lineStart) {
        followingSourceOffset = std::min(fileSize, cursorSourceOffset + lineStart + 1);
      }

      const auto parsed = micromarkd::parseMarkdownLine(sourceLine);
      size_t lineTextOffset = cursorTextOffset;
      cursorTextOffset = 0;  // Only the first source fragment can resume mid-line.
      if (lineTextOffset > parsed.text.size()) lineTextOffset = 0;

      if (parsed.block == micromarkd::BlockKind::Separator || parsed.text.empty()) {
        outLines.push_back(parsed.text);
        outMarkdownLines.push_back(parsed);
        pos = followingSourceOffset - cursorSourceOffset;
        continue;
      }

      if (renderer.isSdCardFont(cachedFontId)) {
        renderer.ensureSdCardFontReady(cachedFontId, parsed.text.c_str(), /*styleMask=*/0x01);
      }

      const int availableWidth = std::max(1, viewportWidth - markdownIndent(parsed.block));
      while (lineTextOffset < parsed.text.size() && static_cast<int>(outLines.size()) < linesPerPage) {
        size_t wrapEnd = findMarkdownWrapEnd(renderer, cachedFontId, parsed, lineTextOffset, availableWidth);
        if (wrapEnd <= lineTextOffset) {
          LOG_ERR("TRS", "Markdown wrapper stalled at %zu:%zu; forcing one UTF-8 code point",
                  cursorSourceOffset + lineStart, lineTextOffset);
          wrapEnd = nextUtf8Boundary(parsed.text, lineTextOffset);
        }
        if (wrapEnd <= lineTextOffset) {
          // A malformed final fragment should not trap page-index construction.
          lineTextOffset = parsed.text.size();
          break;
        }

        auto fragment = micromarkd::sliceParsedLine(parsed, lineTextOffset, wrapEnd - lineTextOffset);
        outLines.push_back(fragment.text);
        outMarkdownLines.push_back(std::move(fragment));

        lineTextOffset = wrapEnd;
        while (lineTextOffset < parsed.text.size() &&
               (parsed.text[lineTextOffset] == ' ' || parsed.text[lineTextOffset] == '\t')) {
          lineTextOffset++;
        }
      }

      if (lineTextOffset < parsed.text.size()) {
        free(buffer);
        nextSourceOffset = cursorSourceOffset + lineStart;
        nextTextOffset = lineTextOffset;
        return !outLines.empty();
      }

      pos = followingSourceOffset - cursorSourceOffset;
    }

    free(buffer);

    if (pos == 0) {
      LOG_ERR("TRS", "Markdown chunk made no source progress at %zu", cursorSourceOffset);
      cursorSourceOffset = std::min(fileSize, cursorSourceOffset + 1);
    } else {
      cursorSourceOffset += pos;
    }
    cursorTextOffset = 0;
  }

  nextSourceOffset = cursorSourceOffset;
  nextTextOffset = cursorTextOffset;
  return !outLines.empty();
}
#endif

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  currentPageLines.clear();
#ifdef MICROMARKD_APP
  if (markdownMode) {
    currentMarkdownLines.clear();
    markdownLinkHits.clear();

    if (currentPage >= static_cast<int>(markdownPageTextOffsets.size())) {
      LOG_ERR("TRS", "Markdown page cursor missing for page %d", currentPage);
      return;
    }

    size_t nextSourceOffset = pageOffsets[currentPage];
    size_t nextTextOffset = markdownPageTextOffsets[currentPage];
    loadMarkdownPageAtCursor(renderer, pageOffsets[currentPage], markdownPageTextOffsets[currentPage], currentPageLines,
                             currentMarkdownLines, nextSourceOffset, nextTextOffset);
  } else
#endif
  {
    size_t nextOffset = pageOffsets[currentPage];
    loadPageAtOffset(renderer, pageOffsets[currentPage], currentPageLines, nextOffset);
#ifdef MICROMARKD_APP
    currentMarkdownLines.clear();
    markdownLinkHits.clear();
#endif
  }

  renderer.clearScreen();
  renderPage(renderer);

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&](const bool recordLinks) {
#ifdef MICROMARKD_APP
    if (recordLinks) markdownLinkHits.clear();
#endif
    int y = cachedOrientedMarginTop;
    for (size_t lineIndex = 0; lineIndex < currentPageLines.size(); lineIndex++) {
      const auto& rawLine = currentPageLines[lineIndex];
      const char* text = rawLine.c_str();
      EpdFontFamily::Style style = EpdFontFamily::REGULAR;
      int indent = 0;
#ifdef MICROMARKD_APP
      bool forceLeft = false;
      bool drawBullet = false;
      bool drawQuote = false;
      bool drawSeparator = false;
#endif

#ifdef MICROMARKD_APP
      const micromarkd::ParsedLine* markdownLine = nullptr;
      if (markdownMode && lineIndex < currentMarkdownLines.size()) {
        markdownLine = &currentMarkdownLines[lineIndex];
        text = markdownLine->text.c_str();
        if (markdownLine->bold) {
          style = EpdFontFamily::BOLD;
        } else if (markdownLine->italic) {
          style = EpdFontFamily::ITALIC;
        }

        switch (markdownLine->block) {
          case micromarkd::BlockKind::Heading:
            forceLeft = true;
            break;
          case micromarkd::BlockKind::Quote:
            indent = 16;
            forceLeft = true;
            drawQuote = true;
            break;
          case micromarkd::BlockKind::Bullet:
            indent = 22;
            forceLeft = true;
            drawBullet = !markdownLine->continuation;
            break;
          case micromarkd::BlockKind::OrderedList:
          case micromarkd::BlockKind::Code:
            indent = 12;
            forceLeft = true;
            break;
          case micromarkd::BlockKind::Separator:
            forceLeft = true;
            drawSeparator = true;
            break;
          case micromarkd::BlockKind::Paragraph:
            break;
        }
      }
#endif

#ifdef MICROMARKD_APP
      if (drawSeparator) {
        renderer.drawLine(cachedOrientedMarginLeft, y + lineHeight / 2, cachedOrientedMarginLeft + contentWidth,
                          y + lineHeight / 2, true);
      } else
#endif
          if (text[0] != '\0') {
        int x = cachedOrientedMarginLeft + indent;
        const bool lineIsRtl = BidiUtils::startsWithRtl(text, BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
#ifdef MICROMARKD_APP
        if (forceLeft) effectiveAlignment = CrossPointSettings::LEFT_ALIGN;
#endif
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, text, style);
        const int availableWidth = contentWidth - indent;

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + indent + (availableWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

#ifdef MICROMARKD_APP
        if (drawQuote) {
          renderer.drawLine(cachedOrientedMarginLeft + 3, y, cachedOrientedMarginLeft + 3, y + lineHeight - 3, 2, true);
        }
#endif
#ifdef MICROMARKD_APP
        if (drawBullet) {
          renderer.drawText(cachedFontId, cachedOrientedMarginLeft + 4, y, "-", true, EpdFontFamily::BOLD);
        }
#endif
        renderer.drawText(cachedFontId, x, y, text, true, style);

#ifdef MICROMARKD_APP
        if (markdownMode && recordLinks && markdownLine != nullptr) {
          for (uint8_t linkIndex = 0; linkIndex < markdownLine->linkCount; linkIndex++) {
            const auto& link = markdownLine->links[linkIndex];
            if (link.end <= link.start || link.end > markdownLine->text.size()) continue;

            const std::string prefix = markdownLine->text.substr(0, link.start);
            const std::string label = markdownLine->text.substr(link.start, link.end - link.start);
            const int linkX = x + renderer.getTextAdvanceX(cachedFontId, prefix.c_str(), style);
            const int linkWidth = renderer.getTextAdvanceX(cachedFontId, label.c_str(), style);
            if (linkWidth <= 0) continue;

            renderer.drawLine(linkX, y + lineHeight - 2, linkX + linkWidth, y + lineHeight - 2, true);
            if (markdownLinkHits.size() < 32) {
              markdownLinkHits.push_back({linkX - 2, y - 2, linkWidth + 4, lineHeight + 4, link.target});
            }
          }
        }
#endif
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines(false);  // scan pass
  renderStatusBar();   // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines(true);
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(false); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::loop() {
  ReaderActivity::loop();
#ifdef MICROMARKD_APP
  if (wikiLinkResolution_) stepWikiLinkResolution();
#endif
}

// Out of line so the unique_ptr members can use forward-declared types.
TxtReaderActivity::~TxtReaderActivity() = default;

bool TxtReaderActivity::handleFormatInput() {
#ifdef MICROMARKD_APP
  if (!markdownMode) return false;

  if (!markdownHistory.empty() && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    const auto previous = markdownHistory.back();
    if (openMarkdownFile(previous.path, previous.page, false)) markdownHistory.pop_back();
    return true;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return false;

  std::string target;
  {
    RenderLock lock(*this);
    for (const auto& hit : markdownLinkHits) {
      if (tapX >= hit.x && tapX < hit.x + hit.width && tapY >= hit.y && tapY < hit.y + hit.height) {
        target = hit.target;
        break;
      }
    }
  }

  if (target.empty()) return false;
  const std::string path = resolveWikiLink(target);
  if (path.empty()) {
    // Direct path/basename lookup failed; fall back to the vault metadata catalog.
    beginWikiLinkIndexResolution(target);
    return true;
  }

  if (path != bookPath) openMarkdownFile(path, -1, true);
  return true;
#else
  return false;
#endif
}

#ifdef MICROMARKD_APP

bool TxtReaderActivity::openMarkdownFile(const std::string& path, const int page, const bool rememberCurrent) {
  if (!Storage.exists(path.c_str()) || !FsHelpers::hasMarkdownExtension(path)) return false;

  auto nextTxt = makeUniqueNoThrow<Txt>(path, "/.crosspoint");
  if (!nextTxt || !nextTxt->load()) {
    LOG_ERR("MD", "Failed to open wikilink target: %s", path.c_str());
    return false;
  }
  nextTxt->setupCacheDir();

  if (rememberCurrent) {
    saveProgress();
    if (markdownHistory.size() >= 16) markdownHistory.erase(markdownHistory.begin());
    markdownHistory.push_back({bookPath, currentPage});
  }

  {
    RenderLock lock(*this);
    txt = std::move(nextTxt);
    bookPath = path;
    currentPage = 0;
    totalPages = 1;
    pageOffsets.clear();
    markdownPageTextOffsets.clear();
    currentPageLines.clear();
    currentMarkdownLines.clear();
    markdownLinkHits.clear();
    pendingMarkdownPage = page;
    initialized = false;
    markdownMode = true;
    endOfBookOptions.reset();
    endOfBookOptionsReady.store(false, std::memory_order_release);
  }

  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, txt->getTitle(), "", "");
  requestUpdate();
  return true;
}

std::string TxtReaderActivity::resolveWikiLink(const std::string& rawTarget) const {
  std::string target = micromarkd::wikiTargetPathPart(rawTarget);
  if (target.empty()) return {};
  std::replace(target.begin(), target.end(), '\\', '/');

  const auto tryCandidate = [](const std::string& rawPath) -> std::string {
    const std::string normalised = FsHelpers::normalisePath(rawPath);
    if (normalised.empty()) return {};
    const std::string path = "/" + normalised;
    if (path != "/vault" && path.rfind("/vault/", 0) != 0) return {};

    if (FsHelpers::hasMarkdownExtension(path)) {
      return Storage.exists(path.c_str()) ? path : std::string{};
    }

    const std::string mdPath = path + ".md";
    if (Storage.exists(mdPath.c_str())) return mdPath;
    const std::string markdownPath = path + ".markdown";
    if (Storage.exists(markdownPath.c_str())) return markdownPath;
    return {};
  };

  if (target.front() == '/') return tryCandidate("/vault/" + target.substr(1));

  const std::string folder = FsHelpers::extractFolderPath(bookPath);
  std::string resolved = tryCandidate(folder + "/" + target);
  if (!resolved.empty()) return resolved;
  return tryCandidate("/vault/" + target);
}

void TxtReaderActivity::beginWikiLinkIndexResolution(const std::string& target) {
  auto resolution = makeUniqueNoThrow<WikiLinkResolution>();
  if (!resolution) return;
  resolution->target = target;
  resolution->sourceBook = bookPath;
  resolution->indexer = makeUniqueNoThrow<MarkdownVaultIndexer>();
  resolution->catalog = makeUniqueNoThrow<micromarkd::MarkdownCatalog>();
  if (!resolution->indexer || !resolution->catalog) {
    LOG_ERR("MD", "OOM: wikilink index resolution");
    return;
  }
  resolution->indexer->begin("/vault");
  wikiLinkResolution_ = std::move(resolution);
}

void TxtReaderActivity::stepWikiLinkResolution() {
  WikiLinkResolution& pending = *wikiLinkResolution_;
  // Navigating to another note invalidates the pending source-relative lookup.
  if (pending.sourceBook != bookPath || !markdownMode) {
    wikiLinkResolution_.reset();
    return;
  }

  if (pending.indexer->hasRecord()) {
    pending.catalog->addRecord(pending.indexer->takeRecord());
  } else if (!pending.indexer->complete()) {
    pending.indexer->step();
    if (pending.indexer->hasRecord()) pending.catalog->addRecord(pending.indexer->takeRecord());
  }
  if (!pending.indexer->complete() || pending.indexer->hasRecord()) return;

  pending.catalog->finalize();
  const std::string path = pending.catalog->resolveTarget(bookPath, pending.target);
  const std::string target = pending.target;
  wikiLinkResolution_.reset();

  if (path.empty() || path == bookPath) {
    LOG_INF("MD", "Wikilink target not found: %s", target.c_str());
    return;
  }
  openMarkdownFile(path, -1, true);
}
#endif

void TxtReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

bool TxtReaderActivity::pageTurn(bool isForward) {
  // Ignore paging until initializeReader has established the page index
  if (!initialized) {
    return false;
  }
  if (isForward) {
    if (currentPage < totalPages) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized) {
    return false;
  }
  int newPage = currentPage + amount;
  if (newPage < 0) newPage = 0;
  // Clamp to totalPages, not totalPages - 1: pageTurn() lets currentPage reach
  // totalPages and isAtEndOfBook() treats that as the end-of-book sentinel, so
  // a forward skip must be able to reach it too.
  if (newPage > totalPages) newPage = totalPages;
  if (newPage != currentPage) {
    currentPage = newPage;
    return true;
  }
  return false;
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && currentPage >= totalPages; }

void TxtReaderActivity::onReturnFromEndOfBook() { currentPage = totalPages > 0 ? totalPages - 1 : 0; }

void TxtReaderActivity::saveProgress() const {
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
#ifdef MICROMARKD_APP
  if (markdownMode) {
    const std::string cachePath = txt->getCachePath() + "/markdown-index.bin";
    HalFile f;
    if (!Storage.openFileForRead("TRS", cachePath, f)) {
      LOG_DBG("TRS", "No measured Markdown page index cache found");
      return false;
    }

    uint32_t magic;
    serialization::readPod(f, magic);
    if (magic != MARKDOWN_CACHE_MAGIC) return false;

    uint8_t version;
    serialization::readPod(f, version);
    if (version != MARKDOWN_CACHE_VERSION) return false;

    uint32_t fileSize;
    serialization::readPod(f, fileSize);
    if (fileSize != txt->getFileSize()) return false;

    int32_t cachedWidth;
    serialization::readPod(f, cachedWidth);
    if (cachedWidth != viewportWidth) return false;

    int32_t cachedLines;
    serialization::readPod(f, cachedLines);
    if (cachedLines != linesPerPage) return false;

    int32_t fontId;
    serialization::readPod(f, fontId);
    if (fontId != cachedFontId) return false;

    int32_t margin;
    serialization::readPod(f, margin);
    if (margin != cachedScreenMargin) return false;

    uint8_t alignment;
    serialization::readPod(f, alignment);
    if (alignment != cachedParagraphAlignment) return false;

    uint32_t numPages;
    serialization::readPod(f, numPages);
    if (numPages == 0 || numPages > txt->getFileSize() + 1) return false;

    pageOffsets.clear();
    markdownPageTextOffsets.clear();
    pageOffsets.reserve(numPages);
    markdownPageTextOffsets.reserve(numPages);

    for (uint32_t i = 0; i < numPages; i++) {
      uint32_t sourceOffset;
      uint32_t textOffset;
      serialization::readPod(f, sourceOffset);
      serialization::readPod(f, textOffset);
      if (sourceOffset > txt->getFileSize()) return false;
      pageOffsets.push_back(sourceOffset);
      markdownPageTextOffsets.push_back(textOffset);
    }

    totalPages = pageOffsets.size();
    LOG_DBG("TRS", "Loaded measured Markdown page index cache: %d pages", totalPages);
    return true;
  }
#endif

  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
#ifdef MICROMARKD_APP
  if (markdownMode) {
    if (pageOffsets.size() != markdownPageTextOffsets.size()) {
      LOG_ERR("TRS", "Refusing to save mismatched Markdown page cursors");
      return;
    }

    const std::string cachePath = txt->getCachePath() + "/markdown-index.bin";
    HalFile f;
    if (!Storage.openFileForWrite("TRS", cachePath, f)) {
      LOG_ERR("TRS", "Failed to save measured Markdown page index cache");
      return;
    }

    serialization::writePod(f, MARKDOWN_CACHE_MAGIC);
    serialization::writePod(f, MARKDOWN_CACHE_VERSION);
    serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
    serialization::writePod(f, static_cast<int32_t>(viewportWidth));
    serialization::writePod(f, static_cast<int32_t>(linesPerPage));
    serialization::writePod(f, static_cast<int32_t>(cachedFontId));
    serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
    serialization::writePod(f, cachedParagraphAlignment);
    serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

    for (size_t i = 0; i < pageOffsets.size(); i++) {
      serialization::writePod(f, static_cast<uint32_t>(pageOffsets[i]));
      serialization::writePod(f, static_cast<uint32_t>(markdownPageTextOffsets[i]));
    }

    LOG_DBG("TRS", "Saved measured Markdown page index cache: %d pages", totalPages);
    return;
  }
#endif

  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
