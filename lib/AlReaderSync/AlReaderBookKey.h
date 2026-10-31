#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace AlReaderSync {

// Build the per-book identifier used by AlReader XE 1.33's AlFiles.calcInitialCRC
// and AlFormat.updateCRC. `firstBytes` must contain the first min(fileSize, 1024)
// bytes of the original file. `bookId` is the optional qualifying UUID parsed from
// EPUB dc:identifier or FB2 <id> (already reduced to 36 lower-case hex/hyphen chars).
std::string makeAlReaderBookKey(const std::string& fullPath, uint64_t fileSize, const uint8_t* firstBytes,
                                size_t firstBytesLength, const std::string& bookId = {});

// Read only the AlReader-significant 1024-byte prefix from the original book file.
// Returns empty if the file cannot be opened or the prefix cannot be read completely.
std::string makeAlReaderBookKeyFromFile(const std::string& fullPath, const std::string& bookId = {});

}  // namespace AlReaderSync
