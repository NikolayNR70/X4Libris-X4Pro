#pragma once

#include "CrossPointSettings.h"

// Drop the publisher-style sentinel from global reader settings while preserving
// explicit stored choices. Called once for settings written before policy version 2.
inline bool migrateLegacyReaderStyleChoices(CrossPointSettings& settings) {
  bool changed = false;
  if (settings.paragraphAlignment == CrossPointSettings::BOOK_STYLE) {
    settings.paragraphAlignment = CrossPointSettings::JUSTIFIED;
    changed = true;
  }
  if (settings.headingAlignment == 0) {
    settings.headingAlignment = 1;  // Center.
    changed = true;
  }
  if (settings.readerLineHeightChoice == 0) {
    settings.readerLineHeightChoice = 1;  // 100%.
    changed = true;
  }
  if (settings.readerIndentChoice == 0) {
    settings.readerIndentChoice = 1;  // 0%.
    changed = true;
  }
  if (settings.readerGapChoice == 0) {
    settings.readerGapChoice = 1;  // 0%.
    changed = true;
  }
  if (settings.readerHeadingSizeChoice == 0) {
    settings.readerHeadingSizeChoice = 2;  // 100%.
    changed = true;
  }
  if (settings.readerMarginChoice == 0) {
    settings.readerMarginChoice = 2;  // 5px, the existing effective default.
    changed = true;
  }
  return changed;
}
