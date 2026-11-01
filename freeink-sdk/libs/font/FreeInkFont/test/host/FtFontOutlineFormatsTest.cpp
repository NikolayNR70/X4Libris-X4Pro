#include <FtFont.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using freeink::font::FtFont;

namespace {
struct MemorySource {
  const std::vector<uint8_t>* bytes;
};

unsigned long readAt(void* context, const unsigned long offset, unsigned char* buffer, const unsigned long count) {
  const auto* source = static_cast<MemorySource*>(context);
  if (!source || !source->bytes || offset >= source->bytes->size()) return 0;
  const size_t available = source->bytes->size() - offset;
  const size_t amount = available < count ? available : count;
  std::copy_n(source->bytes->data() + offset, amount, buffer);
  return static_cast<unsigned long>(amount);
}

bool validate(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (bytes.empty()) return false;
  MemorySource source{&bytes};
  FtFont::FaceInfo info{};
  char family[96]{};
  if (FtFont::inspectStream(&readAt, &source, static_cast<unsigned long>(bytes.size()), info, family, sizeof(family)) !=
          FtFont::InspectResult::Ok ||
      family[0] == '\0') {
    std::fprintf(stderr, "font metadata inspection failed: %s\n", path);
    return false;
  }

  FtFont font;
  if (!font.initStream(&readAt, &source, static_cast<unsigned long>(bytes.size()), 20)) {
    std::fprintf(stderr, "streamed FreeType open failed: %s (stage=%u error=%d)\n", path,
                 static_cast<unsigned>(font.lastInitFailure()), font.lastInitError());
    return false;
  }
  const freeink::font::GlyphBitmap* glyph = font.rasterize('A', 20);
  if (!glyph || !glyph->pixels || glyph->width == 0 || glyph->height == 0) {
    std::fprintf(stderr, "glyph rasterization failed: %s\n", path);
    return false;
  }
#if defined(FREEINK_FONT_ENABLE_AUTOHINT) && FREEINK_FONT_ENABLE_AUTOHINT
  FtFont::RenderOptions options;
  options.hinting = FtFont::HintingMode::Auto;
  options.stemDarkening = true;
  if (!font.setRenderOptions(options)) {
    std::fprintf(stderr, "auto hinting was rejected: %s\n", path);
    return false;
  }
  glyph = font.rasterize('A', 20);
  if (!glyph || !glyph->pixels || glyph->width == 0 || glyph->height == 0) {
    std::fprintf(stderr, "auto hinted glyph rasterization failed: %s\n", path);
    return false;
  }
#endif
  std::printf("PASS %-32s family='%s' weight=%u italic=%u A=%ux%u\n", path, family,
              static_cast<unsigned>(info.weight), info.italic ? 1 : 0,
              static_cast<unsigned>(glyph->width), static_cast<unsigned>(glyph->height));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <font-file> [font-file ...]\n", argv[0]);
    return 2;
  }
  bool ok = true;
  for (int i = 1; i < argc; ++i) ok = validate(argv[i]) && ok;
  return ok ? 0 : 1;
}
