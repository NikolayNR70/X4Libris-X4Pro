#pragma once

#include <string>

#include "VectorFontSupport.h"

class GfxRenderer;
struct SdCardFontFamilyInfo;

#if CROSSPOINT_VECTOR_FONTS
#include <HalStorage.h>
#include <TtfEpdFont.h>
#include <FontCacheManager.h>

#include <array>
#include <memory>
#include <new>

/// Owns one original TTF/OTF face family while it is registered with GfxRenderer.
/// The files stay open because FreeInkFont streams table reads from the SD card.
class RawTtfFontManager {
 public:
  RawTtfFontManager() = default;
  RawTtfFontManager(const RawTtfFontManager&) = delete;
  RawTtfFontManager& operator=(const RawTtfFontManager&) = delete;

  bool load(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize);
  void unload(GfxRenderer& renderer);
  int getFontId(const std::string& familyName, uint8_t pointSize) const;
  const std::string& currentFamilyName() const { return loadedFamilyName_; }
  uint8_t currentPointSize() const { return loadedPointSize_; }

 private:
  static unsigned long readFile(void* context, unsigned long offset, unsigned char* buffer, unsigned long count);
  static int makeFontId(const char* familyName, uint8_t pointSize);

  std::string loadedFamilyName_;
  uint8_t loadedPointSize_ = 0;
  int fontId_ = 0;
  std::array<std::unique_ptr<HalFile>, 4> sourceFiles_{};
  std::unique_ptr<TtfEpdFont> font_;
};
#else
class RawTtfFontManager {
 public:
  bool load(const SdCardFontFamilyInfo&, GfxRenderer&, uint8_t) { return false; }
  void unload(GfxRenderer&) {}
  int getFontId(const std::string&, uint8_t) const { return 0; }
  const std::string& currentFamilyName() const { return emptyName_; }
  uint8_t currentPointSize() const { return 0; }

 private:
  std::string emptyName_;
};
#endif
