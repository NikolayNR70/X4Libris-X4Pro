#pragma once

#include <Epub.h>

#include <cstdint>
#include <string>

namespace Fb2XPathMapper {

// KOReader parses a direct FB2 as /FictionBook/...; the temporary XHTML used
// to render it has a different /body/DocFragment/... tree. These helpers map
// between KOReader's source tree and the reader's chapter-local visible bytes.
std::string findXPathForVisibleOffset(const Epub& epub, int spineIndex, uint32_t visibleOffset);
bool resolveXPath(const Epub& epub, const std::string& xpath, int& spineIndex, uint32_t& visibleOffset,
                  bool& exactMatch);

}  // namespace Fb2XPathMapper
