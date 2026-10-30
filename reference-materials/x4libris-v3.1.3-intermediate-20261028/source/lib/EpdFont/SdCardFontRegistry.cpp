#include "SdCardFontRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "VectorFontSupport.h"

#if CROSSPOINT_VECTOR_FONTS
#include <FtFont.h>
#endif

namespace {
#if CROSSPOINT_VECTOR_FONTS
unsigned long readFontFile(void* context, const unsigned long offset, unsigned char* buffer,
                           const unsigned long count) {
  FsFile* file = static_cast<FsFile*>(context);
  if (!file || !file->seekSet64(offset)) return 0;
  const int read = file->read(buffer, count);
  return read > 0 ? static_cast<unsigned long>(read) : 0;
}

bool hasRawFontExtension(const char* name) {
  const size_t length = strlen(name);
  if (length < 5) return false;
  const char* ext = name + length - 4;
  return (ext[0] == '.' && (ext[1] == 't' || ext[1] == 'T') && (ext[2] == 't' || ext[2] == 'T') &&
          (ext[3] == 'f' || ext[3] == 'F')) ||
         (length >= 5 && ext[0] == '.' && (ext[1] == 'o' || ext[1] == 'O') && (ext[2] == 't' || ext[2] == 'T') &&
          (ext[3] == 'f' || ext[3] == 'F'));
}

bool hasFontStyleSuffix(const char* filename, const char* suffix) {
  const size_t filenameLength = strlen(filename);
  const size_t suffixLength = strlen(suffix);
  const char* extension = strrchr(filename, '.');
  const size_t stemLength = extension ? static_cast<size_t>(extension - filename) : filenameLength;
  if (stemLength < suffixLength) return false;
  const size_t start = stemLength - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i) {
    char ch = filename[start + i];
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    if (ch != suffix[i]) return false;
  }
  return true;
}

uint8_t rawFontStyle(const freeink::font::FtFont::FaceInfo& face, const char* filename) {
  // Some OTFs store italics in typographic subfamily name ID 17, but FreeType
  // exposes the legacy style_flags as Regular. Use the conventional filename
  // suffix as a fallback so separately supplied static styles aren't dropped.
  const bool boldItalic = hasFontStyleSuffix(filename, "bolditalic");
  const bool italic = face.italic || boldItalic || hasFontStyleSuffix(filename, "italic") ||
                      hasFontStyleSuffix(filename, "oblique");
  const bool bold = face.weight >= 600 || boldItalic || hasFontStyleSuffix(filename, "bold");
  return static_cast<uint8_t>(italic ? (bold ? 3 : 2) : (bold ? 1 : 0));
}
#endif
}  // namespace

// --- SdCardFontFamilyInfo helpers ---

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t size, uint8_t style) const {
  for (const auto& f : files) {
    if (f.pointSize == size && f.style == style) return &f;
  }
  return nullptr;
}

bool SdCardFontFamilyInfo::hasSize(uint8_t size) const {
  for (const auto& f : files) {
    if (f.pointSize == size) return true;
  }
  return false;
}

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> sizes;
  for (const auto& f : files) {
    bool found = false;
    for (uint8_t s : sizes) {
      if (s == f.pointSize) {
        found = true;
        break;
      }
    }
    if (!found) sizes.push_back(f.pointSize);
  }
  std::sort(sizes.begin(), sizes.end());
  return sizes;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::pickClosestSize(uint8_t targetPtSize) const {
  const SdCardFontFileInfo* selected = nullptr;
  int bestDiff = INT32_MAX;
  for (const auto& f : files) {
    int diff = std::abs(static_cast<int>(f.pointSize) - static_cast<int>(targetPtSize));
    // Strict < ensures the first scan wins on ties; then tie-break by smaller
    // pointSize to make the choice independent of filesystem enumeration order.
    if (diff < bestDiff || (diff == bestDiff && selected && f.pointSize < selected->pointSize)) {
      bestDiff = diff;
      selected = &f;
    }
  }
  return selected;
}

// --- SdCardFontRegistry ---

bool SdCardFontRegistry::parseFilename(const char* filename, uint8_t& size, uint8_t& style) {
  // V4 naming: <name>_<size>.cpfont (e.g. Bookerly-SD_14.cpfont)
  // Use an ends-with check rather than strstr() so that in-progress downloads
  // like "Foo_14.cpfont.tmp" or backups like "Foo_14.cpfont~" aren't accepted.
  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  const size_t nameLen = strlen(filename);
  if (nameLen <= kExtLen) return false;
  if (strcmp(filename + nameLen - kExtLen, kExt) != 0) return false;
  const char* ext = filename + nameLen - kExtLen;

  size_t baseLen = ext - filename;
  if (baseLen == 0 || baseLen > 127) return false;

  char base[128];
  memcpy(base, filename, baseLen);
  base[baseLen] = '\0';

  char* lastUnderscore = strrchr(base, '_');
  if (!lastUnderscore || lastUnderscore == base) return false;

  const char* sizeStr = lastUnderscore + 1;
  char* endPtr;
  long sizeVal = strtol(sizeStr, &endPtr, 10);
  if (endPtr == sizeStr || *endPtr != '\0' || sizeVal < 1 || sizeVal > 255) return false;
  size = static_cast<uint8_t>(sizeVal);
  style = 0;
  return true;
}

void SdCardFontRegistry::scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family) {
  FsFile dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  char nameBuffer[128];
  while (true) {
    FsFile entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(nameBuffer, sizeof(nameBuffer));
    const auto entryBytes = static_cast<uint32_t>(entry.fileSize());
    entry.close();

    // Skip macOS resource fork files (._*) and other hidden files
    if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

    uint8_t size, style;
    if (!parseFilename(nameBuffer, size, style)) continue;

    SdCardFontFileInfo info;
    info.path = std::string(dirPath) + "/" + nameBuffer;
    info.pointSize = size;
    info.style = style;
    info.fileBytes = entryBytes;
    family.files.push_back(std::move(info));
  }
  dir.close();
}

void SdCardFontRegistry::scanRoot(const char* rootPath) {
  const size_t firstFamilyInRoot = families_.size();
  FsFile root = Storage.open(rootPath);
  if (!root) {
    LOG_DBG("SDREG", "Fonts directory not found: %s", rootPath);
    return;
  }
  if (!root.isDirectory()) {
    LOG_ERR("SDREG", "Fonts path is not a directory: %s", rootPath);
    root.close();
    return;
  }

  char nameBuffer[128];
  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      // Subdirectory = font family
      entry.getName(nameBuffer, sizeof(nameBuffer));
      entry.close();

      // Skip hidden/system directories (macOS ._*, .Trashes, etc.)
      if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

      // Skip in-flight install/rollback dirs (Bookerly__staging, Bookerly__backup, ...)
      // — these may contain valid-looking .cpfont files during a resumable
      // download but must not be exposed as installable families.
      const size_t nameLen = strlen(nameBuffer);
      static constexpr const char* kStagingSuffix = "__staging";
      static constexpr const char* kBackupSuffix = "__backup";
      const size_t stagingLen = strlen(kStagingSuffix);
      const size_t backupLen = strlen(kBackupSuffix);
      if ((nameLen > stagingLen && strcmp(nameBuffer + nameLen - stagingLen, kStagingSuffix) == 0) ||
          (nameLen > backupLen && strcmp(nameBuffer + nameLen - backupLen, kBackupSuffix) == 0)) {
        continue;
      }

      // A family from an earlier root wins. In this root, a CPFont folder wins
      // over raw SFNT files even if FAT returns those regular files first.
      bool alreadyKnown = false;
      size_t rawFamilyIndex = families_.size();
      for (size_t i = 0; i < families_.size(); ++i) {
        if (families_[i].name != nameBuffer) continue;
        if (i < firstFamilyInRoot || !families_[i].vectorFont) {
          alreadyKnown = true;
        } else {
          rawFamilyIndex = i;
        }
        break;
      }
      if (alreadyKnown) {
        LOG_DBG("SDREG", "Skipping duplicate family %s in %s (primary wins)", nameBuffer, rootPath);
        continue;
      }

      SdCardFontFamilyInfo family;
      family.name = nameBuffer;
      std::string subDirPath = std::string(rootPath) + "/" + nameBuffer;
      scanDirectory(subDirPath.c_str(), family);

      if (!family.files.empty()) {
        if (rawFamilyIndex < families_.size()) families_.erase(families_.begin() + rawFamilyIndex);
        families_.push_back(std::move(family));
        LOG_DBG("SDREG", "Found family: %s (%d files) in %s", families_.back().name.c_str(),
                static_cast<int>(families_.back().files.size()), rootPath);
      }
    } else {
      // Users may copy original TTF/OTF files directly into the fonts folder.
      // Inspect the SFNT name/OS2 tables and group style files by their actual
      // family, so the settings list shows one family instead of four files.
      entry.getName(nameBuffer, sizeof(nameBuffer));
#if CROSSPOINT_VECTOR_FONTS
      if (nameBuffer[0] != '.' && nameBuffer[0] != '_' && hasRawFontExtension(nameBuffer)) {
        const unsigned long bytes = static_cast<unsigned long>(entry.fileSize64());
        freeink::font::FtFont::FaceInfo face{};
        char familyName[96] = {};
        const auto result = freeink::font::FtFont::inspectStream(readFontFile, &entry, bytes, face, familyName,
                                                                 sizeof(familyName));
        if (result == freeink::font::FtFont::InspectResult::Ok && familyName[0] != '\0') {
          SdCardFontFamilyInfo* family = nullptr;
          for (auto& known : families_) {
            if (known.name == familyName && known.vectorFont) {
              family = &known;
              break;
            }
          }
          // A CPFont family with the same name is an explicit higher-priority
          // install in the primary root; avoid presenting duplicate choices.
          bool cpFontConflict = false;
          for (const auto& known : families_) {
            if (known.name == familyName && !known.vectorFont) cpFontConflict = true;
          }
          if (!cpFontConflict) {
            if (!family) {
              SdCardFontFamilyInfo discovered;
              discovered.name = familyName;
              discovered.vectorFont = true;
              families_.push_back(std::move(discovered));
              family = &families_.back();
            }
            const uint8_t style = rawFontStyle(face, nameBuffer);
            bool styleSeen = false;
            for (const auto& existing : family->files) {
              if (existing.style == style) styleSeen = true;
            }
            if (!styleSeen) {
              SdCardFontFileInfo info;
              info.path = std::string(rootPath) + "/" + nameBuffer;
              info.pointSize = 0;
              info.style = style;
              info.fileBytes = bytes;
              family->files.push_back(std::move(info));
              LOG_DBG("SDREG", "Found raw font face %s style=%u family=%s", nameBuffer, style, familyName);
            }
          }
        }
      }
#endif
      entry.close();
    }
  }
  root.close();
}

bool SdCardFontRegistry::discover() {
  families_.clear();
  families_.reserve(MAX_SD_FAMILIES);

  // Primary first so it wins on family-name conflicts; the upstream roots are
  // read-only fallbacks — installers/deleters always target only the primary.
  scanRoot(FONTS_DIR);
  scanRoot(FONTS_DIR_UPSTREAM_HIDDEN);
  scanRoot(FONTS_DIR_UPSTREAM_VISIBLE);

  // Sort families alphabetically
  std::sort(families_.begin(), families_.end(),
            [](const SdCardFontFamilyInfo& a, const SdCardFontFamilyInfo& b) { return a.name < b.name; });

  // Cap at MAX_SD_FAMILIES
  if (static_cast<int>(families_.size()) > MAX_SD_FAMILIES) {
    families_.resize(MAX_SD_FAMILIES);
  }

  LOG_DBG("SDREG", "Discovery complete: %d families", static_cast<int>(families_.size()));
  return !families_.empty();
}

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& f : families_) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

int SdCardFontRegistry::getFamilyIndex(const std::string& name) const {
  for (int i = 0; i < static_cast<int>(families_.size()); i++) {
    if (families_[i].name == name) return i;
  }
  return -1;
}
