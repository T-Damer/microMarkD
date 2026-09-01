#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownIndex.h>

#include <cstdint>
#include <string>

bool writeMarkdownIndexRecord(const micromarkd::MarkdownIndexRecord& record);
bool removeMarkdownIndexRecord(const std::string& notePath);
bool markdownIndexCatalogReady(bool* partial = nullptr);
bool writeMarkdownIndexCatalogReady(bool partial = false);
void invalidateMarkdownIndexCatalog();

#endif  // MICROMARKD_APP
