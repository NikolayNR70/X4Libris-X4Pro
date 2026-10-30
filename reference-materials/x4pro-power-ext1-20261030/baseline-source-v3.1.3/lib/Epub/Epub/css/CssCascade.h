#pragma once
#include <HalStorage.h>
#include <string>
#include <string_view>
#include <vector>
#include "CssStyle.h"

namespace CssCascade {
struct Element { std::string tag, classes, id; };
// CSS subset used by reflowable books: descendant/child combinators, compounds,
// ids and universal selectors. Unsupported syntax fails the entire selector.
bool usable(std::string_view selector);
int specificity(std::string_view selector, const std::vector<Element>& path);
CssStyle resolve(FsFile& records, const std::vector<Element>& path, const CssStyle& parent,
                 const std::string& inlineStyle, float emSize, uint32_t count);
}
