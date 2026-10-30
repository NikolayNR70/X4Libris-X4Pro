#include "RawTtfFontManager.h"

#include <GfxRenderer.h>
#include <SdCardFontRegistry.h>

#include <cstring>

#if CROSSPOINT_VECTOR_FONTS
unsigned long RawTtfFontManager::readFile(void* context, const unsigned long offset, unsigned char* buffer,
                                          const unsigned long count) {
  HalFile* file = static_cast<HalFile*>(context);
  if (!file || !file->seekSet64(offset)) return 0;
  const int read = file->read(buffer, count);
  return read > 0 ? static_cast<unsigned long>(read) : 0;
}

int RawTtfFontManager::makeFontId(const char* familyName, const uint8_t pointSize) {
  // Keep raw-outline IDs in a separate hash domain from CPFont's content-derived IDs.
  uint32_t hash = 2166136261u ^ 0x54544631u;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(familyName); *p; ++p) {
    hash = (hash ^ *p) * 16777619u;
  }
  hash = (hash ^ pointSize) * 16777619u;
  const int id = static_cast<int>(hash);
  return id == 0 ? 1 : id;
}

bool RawTtfFontManager::load(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, const uint8_t pointSize) {
  if (family.name == loadedFamilyName_ && pointSize == loadedPointSize_ && font_) return true;
  unload(renderer);

  auto candidate = std::unique_ptr<TtfEpdFont>(new (std::nothrow) TtfEpdFont());
  if (!candidate) return false;

  for (const auto& source : family.files) {
    if (source.style >= sourceFiles_.size() || sourceFiles_[source.style]) continue;
    std::unique_ptr<HalFile> file(new (std::nothrow) HalFile());
    if (!file || !Storage.openFileForRead("TTF", source.path.c_str(), *file)) continue;
    const unsigned long bytes = static_cast<unsigned long>(file->fileSize64());
    if (bytes == 0) continue;
    candidate->addStreamSource(source.style, &RawTtfFontManager::readFile, file.get(), bytes);
    sourceFiles_[source.style] = std::move(file);
  }

  if (!candidate->load(pointSize)) {
    for (auto& file : sourceFiles_) file.reset();
    return false;
  }

  uint32_t candidateId = static_cast<uint32_t>(makeFontId(family.name.c_str(), pointSize));
  const auto& registeredFonts = renderer.getFontMap();
  while (candidateId == 0 || registeredFonts.count(static_cast<int32_t>(candidateId)) != 0) ++candidateId;
  const int id = static_cast<int32_t>(candidateId);
  renderer.replaceFont(id, candidate->family());
  loadedFamilyName_ = family.name;
  loadedPointSize_ = pointSize;
  fontId_ = id;
  font_ = std::move(candidate);
  if (FontCacheManager* cache = renderer.getFontCacheManager()) cache->setTtfFont(font_.get());
  return true;
}

void RawTtfFontManager::unload(GfxRenderer& renderer) {
  if (FontCacheManager* cache = renderer.getFontCacheManager()) cache->setTtfFont(nullptr);
  if (fontId_ != 0) {
    if (FontCacheManager* cache = renderer.getFontCacheManager()) cache->clearCache();
    renderer.removeFont(fontId_);
  }
  font_.reset();
  for (auto& file : sourceFiles_) file.reset();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
  fontId_ = 0;
}

int RawTtfFontManager::getFontId(const std::string& familyName, const uint8_t pointSize) const {
  if (!font_ || familyName != loadedFamilyName_ || pointSize != loadedPointSize_) return 0;
  return fontId_;
}
#endif
