#include <TtfEpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <RawTtfFontManager.h>
#include <SdCardFontRegistry.h>
#include <HalStorage.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace {
struct StreamFile {
  explicit StreamFile(const char* path) : file(path, std::ios::binary | std::ios::ate) {
    if (file) size = static_cast<unsigned long>(file.tellg());
  }
  std::ifstream file;
  unsigned long size = 0;
};

unsigned long readAt(void* context, const unsigned long offset, unsigned char* out, const unsigned long count) {
  auto* source = static_cast<StreamFile*>(context);
  if (!source->file || offset > source->size) return 0;
  source->file.clear();
  source->file.seekg(static_cast<std::streamoff>(offset));
  source->file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));
  return static_cast<unsigned long>(source->file.gcount());
}

bool check(const char* path) {
  StreamFile source(path);
  if (!source.file || source.size == 0) {
    std::cerr << "cannot open source font: " << path << '\n';
    return false;
  }

  TtfEpdFont font;
  font.addStreamSource(TtfEpdFont::Regular, readAt, &source, source.size);
  if (!font.load(18)) {
    std::cerr << "adapter rejected source font: " << path << '\n';
    return false;
  }

  const EpdFontFamily family = font.family();
  const EpdFontData* data = family.getData();
  const EpdGlyphRef glyph = family.getGlyph('A');
  if (!glyph || !data->glyphBitmapHandler || glyph.sdRecord == nullptr) {
    std::cerr << "dynamic glyph callback did not provide A: " << path << '\n';
    return false;
  }
  const uint8_t* bitmap = data->glyphBitmapHandler(data->glyphMissCtx, glyph.sdRecord);
  if (!bitmap || glyph.width == 0 || glyph.height == 0 || glyph.advanceX == 0) {
    std::cerr << "glyph metrics/bitmap invalid: " << path << '\n';
    return false;
  }

  for (const uint32_t codepoint : {0x0410u, 0x044Fu, 0x0451u}) {  // Cyrillic А, я, ё
    const EpdGlyphRef cyrillic = family.getGlyph(codepoint);
    const uint8_t* cyrillicBitmap = cyrillic && data->glyphBitmapHandler
                                        ? data->glyphBitmapHandler(data->glyphMissCtx, cyrillic.sdRecord)
                                        : nullptr;
    if (!cyrillic || !cyrillicBitmap || cyrillic.width == 0 || cyrillic.height == 0) {
      std::cerr << "Cyrillic glyph U+" << std::hex << codepoint << std::dec << " did not rasterize: " << path << '\n';
      return false;
    }
  }

  int width = 0;
  int height = 0;
  family.getTextDimensions("AV", &width, &height);
  if (width <= 0 || height <= 0) {
    std::cerr << "font-family layout metrics invalid: " << path << '\n';
    return false;
  }

  const EpdGlyphRef bold = family.getGlyph('A', EpdFontFamily::BOLD);
  const EpdGlyphRef italic = family.getGlyph('A', EpdFontFamily::ITALIC);
  const EpdGlyphRef boldItalic = family.getGlyph('A', EpdFontFamily::BOLD_ITALIC);
  if (!bold || !italic || !boldItalic) {
    std::cerr << "style fallback failed: " << path << '\n';
    return false;
  }

  const unsigned originalWidth = glyph.width;
  const unsigned originalHeight = glyph.height;
  if (!font.load(36)) {
    std::cerr << "font failed to reload at a larger point size: " << path << '\n';
    return false;
  }
  const EpdFontFamily largerFamily = font.family();
  const EpdGlyphRef largerGlyph = largerFamily.getGlyph('A');
  const EpdFontData* largerData = largerFamily.getData();
  const uint8_t* largerBitmap = largerGlyph && largerData && largerData->glyphBitmapHandler
                                    ? largerData->glyphBitmapHandler(largerData->glyphMissCtx, largerGlyph.sdRecord)
                                    : nullptr;
  if (!largerGlyph || !largerBitmap || largerGlyph.width <= originalWidth || largerGlyph.height <= originalHeight) {
    std::cerr << "larger point size did not produce a larger rendered glyph: " << path << '\n';
    return false;
  }

  std::cout << "PASS " << path << " bytes=" << source.size << " A=" << originalWidth << 'x' << originalHeight
            << " Cyrillic=3 layout=" << width << 'x' << height << " styles=4 scaled=18->36pt\n";
  return true;
}

bool checkRegistry(const std::vector<std::string>& paths) {
  const auto root = std::filesystem::temp_directory_path() / "witchhunt-tt-font-registry-test";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "primary");
  std::filesystem::create_directories(root / "visible");

  for (const auto& source : paths) {
    const auto filename = std::filesystem::path(source).filename();
    const bool ubuntu = filename.string().rfind("Ubuntu-", 0) == 0;
    const auto directory = root / (ubuntu ? "primary" : "visible");
    std::filesystem::copy_file(source, directory / filename, std::filesystem::copy_options::overwrite_existing);
  }

  // The fake directory iterator deliberately returns regular font files before
  // subdirectories. A same-family CPFont folder must still outrank raw SFNT
  // files, independent of SD/FAT enumeration order.
  std::filesystem::create_directories(root / "primary" / "Ubuntu");
  std::ofstream(root / "primary" / "Ubuntu" / "Ubuntu_18.cpfont", std::ios::binary).put('\0');
  HalStorage::setTestRoot(root);

  SdCardFontRegistry registry;
  if (!registry.discover() || registry.getFamilyCount() != 3) {
    std::cerr << "font registry family count mismatch\n";
    std::filesystem::remove_all(root, ignored);
    return false;
  }
  const auto* ubuntu = registry.findFamily("Ubuntu");
  if (!ubuntu || ubuntu->vectorFont || ubuntu->files.size() != 1 || ubuntu->files[0].pointSize != 18) {
    std::cerr << "primary CPFont did not retain precedence over raw TTF\n";
    std::filesystem::remove_all(root, ignored);
    return false;
  }
  const auto* dyslexic = registry.findFamily("OpenDyslexic");
  if (!dyslexic || !dyslexic->vectorFont || dyslexic->files.size() != 4) {
    std::cerr << "raw OTF family was not grouped into one entry\n";
    for (const auto& family : registry.getFamilies()) {
      std::cerr << "  family='" << family.name << "' vector=" << family.vectorFont
                << " files=" << family.files.size() << '\n';
      for (const auto& face : family.files) std::cerr << "    style=" << unsigned(face.style) << " path=" << face.path << '\n';
    }
    std::filesystem::remove_all(root, ignored);
    return false;
  }
  const auto* inter = registry.findFamily("Inter");
  if (!inter || !inter->vectorFont || inter->files.size() != 2) {
    std::cerr << "raw TTF family was not discovered as one family with two source styles\n";
    std::filesystem::remove_all(root, ignored);
    return false;
  }
  bool styles[4] = {};
  for (const auto& face : dyslexic->files) {
    if (face.pointSize != 0 || face.style >= 4 || styles[face.style] || face.path.rfind("/fonts/", 0) != 0) {
      std::cerr << "raw OTF face metadata/path invalid\n";
      std::filesystem::remove_all(root, ignored);
      return false;
    }
    styles[face.style] = true;
  }
  if (!std::all_of(std::begin(styles), std::end(styles), [](bool present) { return present; })) {
    std::cerr << "raw OTF family style roles missing\n";
    std::filesystem::remove_all(root, ignored);
    return false;
  }

  GfxRenderer renderer;
  RawTtfFontManager manager;
  uint32_t occupiedId = 2166136261u ^ 0x54544631u;
  for (const uint8_t* byte = reinterpret_cast<const uint8_t*>("Inter"); *byte; ++byte) {
    occupiedId = (occupiedId ^ *byte) * 16777619u;
  }
  occupiedId = (occupiedId ^ 18u) * 16777619u;
  const int reservedId = static_cast<int32_t>(occupiedId);
  renderer.replaceFont(reservedId, EpdFontFamily(nullptr));
  const SdCardFontFamilyInfo* runtimeFamilies[] = {inter, dyslexic};
  for (const SdCardFontFamilyInfo* runtimeFamily : runtimeFamilies) {
    if (!manager.load(*runtimeFamily, renderer, 18)) {
      std::cerr << "production raw-font manager could not stream the discovered SD family: "
                << runtimeFamily->name << '\n';
      std::filesystem::remove_all(root, ignored);
      return false;
    }
    const int fontId = manager.getFontId(runtimeFamily->name, 18);
    const EpdFontFamily* loaded = renderer.findFont(fontId);
    if (fontId == 0 || !loaded || renderer.registeredFontCount() != 2 ||
        (runtimeFamily == inter && fontId == reservedId)) {
      std::cerr << "raw-font manager did not register the family with the renderer\n";
      manager.unload(renderer);
      std::filesystem::remove_all(root, ignored);
      return false;
    }
    if (renderer.getFontCacheManager()->ttfFont() == nullptr) {
      std::cerr << "raw-font manager did not register its cache owner\n";
      manager.unload(renderer);
      std::filesystem::remove_all(root, ignored);
      return false;
    }
    for (uint8_t style = 0; style < 4; ++style) {
      const auto role = static_cast<EpdFontFamily::Style>(style);
      const EpdGlyphRef glyph = loaded->getGlyph('A', role);
      const EpdFontData* data = loaded->getData(role);
      const uint8_t* bitmap = glyph && data && data->glyphBitmapHandler
                                  ? data->glyphBitmapHandler(data->glyphMissCtx, glyph.sdRecord)
                                  : nullptr;
      if (!glyph || !bitmap || glyph.width == 0 || glyph.height == 0) {
        std::cerr << "registered raw SD family failed glyph rendering for style " << unsigned(style)
                  << " in " << runtimeFamily->name << '\n';
        manager.unload(renderer);
        std::filesystem::remove_all(root, ignored);
        return false;
      }
    }
    renderer.getFontCacheManager()->ttfFont()->releaseResidentCaches();
    for (uint8_t style = 0; style < 4; ++style) {
      const auto role = static_cast<EpdFontFamily::Style>(style);
      const EpdGlyphRef glyph = loaded->getGlyph('A', role);
      const EpdFontData* data = loaded->getData(role);
      const uint8_t* bitmap = glyph && data && data->glyphBitmapHandler
                                  ? data->glyphBitmapHandler(data->glyphMissCtx, glyph.sdRecord)
                                  : nullptr;
      if (!glyph || !bitmap || glyph.width == 0 || glyph.height == 0) {
        std::cerr << "raw font failed to fault style back in after releasing its resident cache\n";
        manager.unload(renderer);
        std::filesystem::remove_all(root, ignored);
        return false;
      }
    }
    manager.unload(renderer);
    if (renderer.findFont(fontId) != nullptr || renderer.registeredFontCount() != 1 ||
        renderer.getFontCacheManager()->ttfFont() != nullptr) {
      std::cerr << "raw-font manager failed to unregister the family on unload\n";
      std::filesystem::remove_all(root, ignored);
      return false;
    }
  }
  renderer.removeFont(reservedId);

  std::cout << "PASS raw SD registry -> manager -> renderer -> TTF and CFF OTF; CPFont precedence preserved\n";
  std::filesystem::remove_all(root, ignored);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: ttf-epd-adapter-test <TTF/OTF> [OTF ...]\n";
    return 2;
  }
  bool ok = true;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    paths.emplace_back(argv[i]);
    ok = check(argv[i]) && ok;
  }
  ok = checkRegistry(paths) && ok;
  return ok ? 0 : 1;
}
