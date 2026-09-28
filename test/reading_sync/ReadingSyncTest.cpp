#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "ReadingSync.h"

using namespace readingsync;

namespace {

const char* kCatalog = R"({
  "version": 2,
  "books": [
    {"id": "0123456789abcdef0123456789abcdef", "title": "Роман", "format": "epub", "library": "personal",
     "source": {"path": "Books/files/personal/Роман.epub", "gitSha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "bytes": 4, "sha256": "x"},
     "xteink": [], "positionMap": "epub-percent"},
    {"id": "fedcba9876543210fedcba9876543210", "title": "Руководство", "format": "pdf", "library": "medical",
     "source": {"path": "Books/files/medical/Руководство.pdf", "gitSha": "b", "bytes": 4, "sha256": "y"},
     "xteink": [
       {"path": "Books/files/medical/Руководство (часть 1 из 2).xtc", "gitSha": "c", "bytes": 4, "sha256": "z", "firstPage": 1},
       {"path": "Books/files/medical/Руководство (часть 2 из 2).xtc", "gitSha": "d", "bytes": 4, "sha256": "w", "firstPage": 961}
     ], "positionMap": "pdf-page"}
  ]
})";

// Exactly what the Obsidian plugin writes.
const char* kPhoneFile = R"({
  "version": 1,
  "bookId": "0123456789abcdef0123456789abcdef",
  "title": "Роман",
  "device": {"id": "mobile-3fa9c1", "platform": "mobile"},
  "position": {"updatedAt": 1785062526961, "pct": 0.42, "block": 191},
  "highlights": {"h1": {"updatedAt": 1785062500000, "data": {"id": "h1", "block": 12, "occ": 0, "text": "…"}}}
})";

}  // namespace

TEST(ReadingSync, FindsBooksBySourceOrXteinkCopy) {
  BookRef book;
  ASSERT_TRUE(findBook(kCatalog, "Books/files/personal/Роман.epub", book));
  EXPECT_EQ(book.id, "0123456789abcdef0123456789abcdef");
  EXPECT_FALSE(book.pdf);

  ASSERT_TRUE(findBook(kCatalog, "Books/files/medical/Руководство (часть 2 из 2).xtc", book));
  EXPECT_EQ(book.id, "fedcba9876543210fedcba9876543210");
  EXPECT_TRUE(book.pdf);
  EXPECT_EQ(book.firstPage, 961);
  EXPECT_TRUE(book.multiVolume);

  EXPECT_FALSE(findBook(kCatalog, "Books/files/personal/Другое.epub", book));
  EXPECT_FALSE(findBook("{\"version\":1,\"books\":[]}", "Books/files/personal/Роман.epub", book));
}

TEST(ReadingSync, ScansALargeCatalogInSmallReads) {
  // ~400 books, as after the Boox import: far more than the device could parse at once.
  std::string catalog = R"({"version": 2, "files": {"repository": "T-Damer/ZettleKasten-library", "branch": "books"},
    "books": [)";
  for (int i = 0; i < 400; i++) {
    char id[33];
    snprintf(id, sizeof(id), "%032x", i + 1);
    if (i) catalog += ",";
    catalog +=
        std::string(R"({"id": ")") + id + R"(", "title": "Книга \")" + std::to_string(i) +
        R"(\" {с} [скобками]", "format": "epub", "library": "gold", "source": {"path": "Books/files/gold/Книга )" +
        std::to_string(i) +
        R"(.epub", "gitSha": "a", "bytes": 1, "sha256": "b"}, "xteink": [], "positionMap": "epub-percent"})";
  }
  catalog += "]}";
  struct Source {
    const std::string* text;
    size_t offset = 0;
    size_t largestRead = 0;
  } source{&catalog};
  const auto read = [](void* context, char* buffer, size_t capacity) {
    auto* s = static_cast<Source*>(context);
    capacity = std::min<size_t>(capacity, 7);  // tokens split across reads
    const size_t n = std::min(capacity, s->text->size() - s->offset);
    memcpy(buffer, s->text->data() + s->offset, n);
    s->offset += n;
    s->largestRead = std::max(s->largestRead, n);
    return n;
  };
  BookRef book;
  ASSERT_TRUE(findBook(CatalogReader{&source, read}, "Books/files/gold/Книга 399.epub", book));
  EXPECT_EQ(book.id, "00000000000000000000000000000190");
  EXPECT_EQ(book.title, "Книга \"399\" {с} [скобками]");
  EXPECT_LE(source.largestRead, 7u);
  EXPECT_TRUE(findBook(catalog, "Books/files/gold/Книга 1.epub", book));

  EXPECT_EQ(filesRepository(catalog), "T-Damer/ZettleKasten-library");
  EXPECT_EQ(filesRepository(kCatalog), "");
  EXPECT_EQ(filesRepository(R"({"version": 2, "files": {"repository": "x/y z", "branch": "books"}, "books": []})"), "");
  EXPECT_EQ(filesRepository(R"({"version": 3, "files": {"repository": "a/b", "branch": "books"}, "books": []})"), "");
}

TEST(ReadingSync, ReadsThePluginFormat) {
  Position position;
  ASSERT_TRUE(readPosition(kPhoneFile, "0123456789abcdef0123456789abcdef", "mobile-3fa9c1", position));
  EXPECT_EQ(position.updatedAt, 1785062526961LL);
  EXPECT_NEAR(position.pct, 0.42, 1e-6);
  EXPECT_EQ(position.page, 0);
  EXPECT_FALSE(readPosition(kPhoneFile, "0123456789abcdef0123456789abcdef", "desktop-111111", position));
  EXPECT_FALSE(readPosition(kPhoneFile, "ffffffffffffffffffffffffffffffff", "mobile-3fa9c1", position));
}

TEST(ReadingSync, PicksTheNewestOtherDevice) {
  BookRef book;
  ASSERT_TRUE(findBook(kCatalog, "Books/files/personal/Роман.epub", book));
  const std::string self = "xteink-a1b2c3";
  const std::vector<DeviceFile> files = {
      {"mobile-3fa9c1", kPhoneFile},
      {"desktop-000001", writeDeviceFile(book, "desktop-000001", {1785062000000LL, 0.3, 0, ""}, "")},
      {self, writeDeviceFile(book, self, {1799999999999LL, 0.9, 0, ""}, "")},
  };
  Position newest;
  ASSERT_TRUE(newestFromOthers(files, book.id, self, newest));
  EXPECT_EQ(newest.deviceId, "mobile-3fa9c1");
  EXPECT_NEAR(newest.pct, 0.42, 1e-6);
}

TEST(ReadingSync, WritesItsFileAndKeepsOtherFields) {
  BookRef book;
  ASSERT_TRUE(findBook(kCatalog, "Books/files/medical/Руководство (часть 1 из 2).xtc", book));
  const std::string self = "xteink-a1b2c3";
  const std::string previous = R"({"version":1,"bookId":"fedcba9876543210fedcba9876543210",
    "device":{"id":"xteink-a1b2c3","platform":"xteink"},
    "position":{"updatedAt":1,"pct":0.1},"highlights":{"k":{"updatedAt":2,"deleted":true}},"extra":7})";
  const std::string json = writeDeviceFile(book, self, {1785062600000LL, 0.25, 480, ""}, previous);
  Position back;
  ASSERT_TRUE(readPosition(json, book.id, self, back));
  EXPECT_EQ(back.updatedAt, 1785062600000LL);
  EXPECT_NEAR(back.pct, 0.25, 1e-6);
  EXPECT_EQ(back.page, 480);
  EXPECT_NE(json.find("\"deleted\": true"), std::string::npos);
  EXPECT_NE(json.find("\"extra\": 7"), std::string::npos);
  EXPECT_NE(json.find("\"platform\": \"xteink\""), std::string::npos);

  // A previous file for another device is not merged in.
  const std::string fresh = writeDeviceFile(book, self, {1785062600000LL, 0.25, 0, ""}, kPhoneFile);
  EXPECT_EQ(fresh.find("h1"), std::string::npos);
}
