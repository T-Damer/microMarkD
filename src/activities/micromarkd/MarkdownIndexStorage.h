#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownIndex.h>

#include <cstdint>
#include <string>

bool writeMarkdownIndexRecord(const micromarkd::MarkdownIndexRecord& record);
bool loadMarkdownIndexRecord(const std::string& notePath, uint64_t sourceSize, micromarkd::MarkdownIndexRecord& record);
bool removeMarkdownIndexRecord(const std::string& notePath);

#endif  // MICROMARKD_APP
