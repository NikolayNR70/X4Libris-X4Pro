#pragma once

#include <string>

namespace AlReaderSync {
// Extract the UUID-shaped identifier AlReader adds to its synchronization key.
// Empty means that the book has no such identifier, or its metadata cannot be read.
std::string findAlReaderBookId(const std::string& path);
}
